#!/usr/bin/env python3
import gzip
import io
import re
import sys
from pathlib import Path


_PRE_TOKEN_RE = re.compile(r"(<(?:pre|textarea)\b[^>]*>.*?</(?:pre|textarea)>)", re.IGNORECASE | re.DOTALL)
_STYLE_RE = re.compile(r"(<style\b[^>]*>)(.*?)(</style>)", re.IGNORECASE | re.DOTALL)
_SCRIPT_RE = re.compile(r"(<script\b[^>]*>)(.*?)(</script>)", re.IGNORECASE | re.DOTALL)
_PARTIAL_HTML_FILES = {"footer.html", "themes.html", "toolbar.html"}
_STATIC_HTML_PAGES = {"licenses_page.html", "settings_page.html", "setup_page.html", "stats_page.html"}


def c_string_literal(text: str) -> str:
    escaped = (
        text.replace("\\", "\\\\")
        .replace("\"", "\\\"")
        .replace("\r", "")
        .replace("\n", "\\n\"\n\"")
    )
    return f"\"{escaped}\""


def c_byte_array(data: bytes) -> str:
    rows = []
    for index in range(0, len(data), 12):
        chunk = data[index:index + 12]
        rows.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk))
    return "{\n" + ",\n".join(rows) + "\n}"


def gzip_bytes(data: bytes) -> bytes:
    out = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=out, compresslevel=9, mtime=0) as gz:
        gz.write(data)
    return out.getvalue()


def symbol_name(path: Path) -> str:
    stem = re.sub(r"[^A-Za-z0-9]+", "_", path.stem).strip("_").upper()
    suffix = path.suffix.lower().lstrip(".").upper()
    return f"WEB_{stem}_{suffix}"


def minify_css(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"\s+", " ", text)
    text = re.sub(r"\s*([{}:;,>~+])\s*", r"\1", text)
    text = re.sub(r";}", "}", text)
    text = text.replace(" 0.", " .")
    return text.strip()


def minify_js(text: str) -> str:
    protected = []

    def protect(match: re.Match) -> str:
        protected.append(match.group(0))
        return f"@@JS_STRING_{len(protected) - 1}@@"

    text = re.sub(r"`(?:\\.|[^`\\])*`|'(?:\\.|[^'\\])*'|\"(?:\\.|[^\"\\])*\"", protect, text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"(^|[^:])//.*", r"\1", text)
    text = re.sub(r"\s+", " ", text)
    text = re.sub(r"\s*([{}()[\];,:?=<>+\-*/%!|&])\s*", r"\1", text)
    for index, value in enumerate(protected):
        text = text.replace(f"@@JS_STRING_{index}@@", value)
    return text.strip()


def minify_html(text: str) -> str:
    protected = []

    def protect(match: re.Match) -> str:
        protected.append(match.group(0))
        return f"@@HTML_BLOCK_{len(protected) - 1}@@"

    def minify_style(match: re.Match) -> str:
        return match.group(1) + minify_css(match.group(2)) + match.group(3)

    def minify_script(match: re.Match) -> str:
        return match.group(1) + minify_js(match.group(2)) + match.group(3)

    text = _PRE_TOKEN_RE.sub(protect, text)
    text = re.sub(r"<!--.*?-->", "", text, flags=re.DOTALL)
    text = _STYLE_RE.sub(minify_style, text)
    text = _SCRIPT_RE.sub(minify_script, text)
    text = re.sub(r"\s+", " ", text)
    text = re.sub(r">\s+<", "><", text)
    text = re.sub(r"\s+>", ">", text)
    text = text.strip()
    for index, value in enumerate(protected):
        text = text.replace(f"@@HTML_BLOCK_{index}@@", value)
    return text


def minify_asset(path: Path, text: str) -> str:
    if path.suffix.lower() == ".css":
        return minify_css(text)
    if path.suffix.lower() == ".html":
        return minify_html(text)
    return text.strip()


def expand_placeholders(text: str, values: dict[str, str], depth: int = 0) -> str:
    if depth >= 4:
        return text

    def replace(match: re.Match) -> str:
        key = match.group(1)
        value = values.get(key)
        if value is None:
            return match.group(0)
        if "{{" in value:
            return expand_placeholders(value, values, depth + 1)
        return value

    return re.sub(r"\{\{([A-Z0-9_]+)\}\}", replace, text)


def static_html_values(relative: Path, assets: dict[str, str], styles_link: str) -> dict[str, str]:
    stats_active = "active" if relative.name == "stats_page.html" else ""
    settings_active = "active" if relative.name in {"settings_page.html", "setup_page.html"} else ""
    return {
        "GLOBAL_STYLES": styles_link,
        "FOOTER": assets.get("footer.html", ""),
        "TOOLBAR": assets.get("toolbar.html", ""),
        "THEMES": assets.get("themes.html", ""),
        "THEME": "dark",
        "STATS_ACTIVE": stats_active,
        "SETTINGS_ACTIVE": settings_active,
    }


def main() -> int:
    if len(sys.argv) < 4:
        print("usage: embed_web_assets.py OUT WEB_DIR FILE...", file=sys.stderr)
        return 2

    out_path = Path(sys.argv[1])
    web_dir = Path(sys.argv[2])
    files = [Path(arg) for arg in sys.argv[3:]]
    minified_assets = {}
    relative_paths = {}
    for file_path in files:
        try:
            relative = file_path.relative_to(web_dir)
        except ValueError:
            relative = Path(file_path.name)
        relative_paths[file_path] = relative
        if relative.suffix.lower() == ".txt":
            continue
        minified_assets[relative.name] = minify_asset(relative, file_path.read_text(encoding="utf-8"))

    styles_text = minified_assets.get("styles.css", "")
    styles_link = '<link rel="stylesheet" href="/styles.css">'

    lines = [
        "#pragma once",
        "",
        "/* Generated by scripts/tools/embed_web_assets.py. Do not edit directly. */",
        "",
    ]
    for file_path in files:
        relative = relative_paths[file_path]
        name = symbol_name(relative)
        # Only standalone static text assets are pre-gzipped here. Display assets
        # such as LCD backgrounds use their own converter into display-native RGB565.
        if relative.suffix.lower() == ".txt":
            compressed = gzip_bytes(file_path.read_bytes())
            lines.append(f"static const unsigned char {name}_GZ[] =")
            lines.append(c_byte_array(compressed) + ";")
            lines.append(f"static const unsigned int {name}_GZ_LEN = {len(compressed)}U;")
            lines.append("")
            continue
        text = minified_assets[relative.name]
        if relative.name == "styles.css":
            compressed = gzip_bytes(styles_text.encode("utf-8"))
            lines.append(f"static const unsigned char {name}_GZ[] =")
            lines.append(c_byte_array(compressed) + ";")
            lines.append(f"static const unsigned int {name}_GZ_LEN = {len(compressed)}U;")
            lines.append("")
            continue
        if relative.name in _PARTIAL_HTML_FILES:
            continue
        if relative.name in _STATIC_HTML_PAGES:
            text = expand_placeholders(text, static_html_values(relative, minified_assets, styles_link))
            if "{{" not in text:
                compressed = gzip_bytes(text.encode("utf-8"))
                lines.append(f"static const unsigned char {name}_GZ[] =")
                lines.append(c_byte_array(compressed) + ";")
                lines.append(f"static const unsigned int {name}_GZ_LEN = {len(compressed)}U;")
                lines.append("")
                continue
        lines.append(f"static const char {name}[] =")
        lines.append(c_string_literal(text) + ";")
        lines.append("")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
