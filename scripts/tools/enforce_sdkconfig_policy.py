#!/usr/bin/env python3
"""Keep generated sdkconfig aligned with firmware scheduling policy."""

from __future__ import annotations

import argparse
from pathlib import Path


POLICY = {
    "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0": "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y",
    "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1": "# CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 is not set",
    "CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU0": "CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU0=y",
    "CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU1": "# CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU1 is not set",
    "CONFIG_FREERTOS_TIMER_TASK_NO_AFFINITY": "# CONFIG_FREERTOS_TIMER_TASK_NO_AFFINITY is not set",
    "CONFIG_FREERTOS_TIMER_SERVICE_TASK_CORE_AFFINITY": "CONFIG_FREERTOS_TIMER_SERVICE_TASK_CORE_AFFINITY=0x0",
    "CONFIG_LWIP_TCPIP_TASK_AFFINITY_NO_AFFINITY": "# CONFIG_LWIP_TCPIP_TASK_AFFINITY_NO_AFFINITY is not set",
    "CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0": "CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0=y",
    "CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU1": "# CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU1 is not set",
    "CONFIG_LWIP_TCPIP_TASK_AFFINITY": "CONFIG_LWIP_TCPIP_TASK_AFFINITY=0x0",
    "CONFIG_ESP_TIMER_TASK_AFFINITY": "CONFIG_ESP_TIMER_TASK_AFFINITY=0x0",
    "CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0": "CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0=y",
    "CONFIG_ESP_TIMER_TASK_AFFINITY_CPU1": "# CONFIG_ESP_TIMER_TASK_AFFINITY_CPU1 is not set",
    "CONFIG_ESP_TIMER_TASK_AFFINITY_NO_AFFINITY": "# CONFIG_ESP_TIMER_TASK_AFFINITY_NO_AFFINITY is not set",
}

POLICY_ORDER = [
    "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0",
    "CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1",
    "CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU0",
    "CONFIG_FREERTOS_TIMER_TASK_AFFINITY_CPU1",
    "CONFIG_FREERTOS_TIMER_TASK_NO_AFFINITY",
    "CONFIG_FREERTOS_TIMER_SERVICE_TASK_CORE_AFFINITY",
    "CONFIG_LWIP_TCPIP_TASK_AFFINITY_NO_AFFINITY",
    "CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0",
    "CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU1",
    "CONFIG_LWIP_TCPIP_TASK_AFFINITY",
    "CONFIG_ESP_TIMER_TASK_AFFINITY",
    "CONFIG_ESP_TIMER_TASK_AFFINITY_CPU0",
    "CONFIG_ESP_TIMER_TASK_AFFINITY_CPU1",
    "CONFIG_ESP_TIMER_TASK_AFFINITY_NO_AFFINITY",
]

POLICY_MARKER = "# M45 scheduling policy: keep network/system work off the miner core."


def line_symbol(line: str) -> str | None:
    if line.startswith("CONFIG_"):
        return line.split("=", 1)[0].strip()
    if line.startswith("# CONFIG_") and line.rstrip().endswith(" is not set"):
        return line.removeprefix("# ").removesuffix(" is not set\n").removesuffix(" is not set")
    return None


def line_value(line: str) -> tuple[str, str | None] | None:
    symbol = line_symbol(line)
    if symbol is None:
        return None
    if line.startswith("CONFIG_"):
        return symbol, line.split("=", 1)[1].strip()
    return symbol, None


def is_policy_line(line: str) -> bool:
    return line.rstrip("\n") == POLICY_MARKER or line_symbol(line) in POLICY


def desired_value(line: str) -> str | None:
    if line.startswith("CONFIG_"):
        return line.split("=", 1)[1]
    return None


def policy_satisfied(lines: list[str]) -> bool:
    values: dict[str, str | None] = {}
    for line in lines:
        parsed = line_value(line)
        if parsed is not None:
            symbol, value = parsed
            values[symbol] = value

    for symbol, line in POLICY.items():
        expected = desired_value(line)
        actual = values.get(symbol)
        if expected is None:
            if actual is not None:
                return False
            continue
        if actual != expected:
            return False
    return True


def enforce(path: Path) -> bool:
    if not path.exists():
        return False

    original = path.read_text(encoding="utf-8").splitlines(keepends=True)
    if policy_satisfied(original):
        return False

    filtered = [line for line in original if not is_policy_line(line)]
    while filtered and filtered[-1].strip() == "":
        filtered.pop()

    policy_block = [
        "\n",
        f"{POLICY_MARKER}\n",
        *[f"{POLICY[symbol]}\n" for symbol in POLICY_ORDER],
    ]
    updated = filtered + policy_block
    if updated == original:
        return False

    path.write_text("".join(updated), encoding="utf-8")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("sdkconfig", type=Path)
    args = parser.parse_args()
    if enforce(args.sdkconfig):
        print(f"updated {args.sdkconfig}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
