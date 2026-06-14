#!/usr/bin/env python3
"""Minimal independently-verifying Stratum V1 server for miner audits."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import socketserver
import threading
import time
import urllib.request
from dataclasses import dataclass, field
from decimal import Decimal, getcontext
from typing import Any


DIFF1_TARGET = int(
    "00000000ffff0000000000000000000000000000000000000000000000000000", 16
)
MAX_TARGET = (1 << 256) - 1
JOB_ID = "audit-1"
EXTRANONCE1 = "00000000"
EXTRANONCE2_SIZE = 4
PREVHASH = "00" * 32
COINBASE1 = "01000000"
COINBASE2 = "ffffffff"
VERSION = "20000000"
NBITS = "1d00ffff"
NTIME = "65000000"


def sha256d(data: bytes) -> bytes:
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def word_swap_32(hex_text: str) -> bytes:
    raw = bytes.fromhex(hex_text)
    if len(raw) != 32:
        raise ValueError("previous hash must be 32 bytes")
    return b"".join(raw[offset : offset + 4][::-1] for offset in range(0, 32, 4))


def difficulty_target(difficulty: Decimal) -> int:
    if difficulty <= 0:
        raise ValueError("difficulty must be positive")
    target = int(Decimal(DIFF1_TARGET) / difficulty)
    return min(target, MAX_TARGET)


def build_header(extranonce2: str, ntime: str, nonce: str) -> bytes:
    coinbase = bytes.fromhex(COINBASE1 + EXTRANONCE1 + extranonce2 + COINBASE2)
    merkle = sha256d(coinbase)
    return (
        bytes.fromhex(VERSION)[::-1]
        + word_swap_32(PREVHASH)
        + merkle
        + bytes.fromhex(ntime)[::-1]
        + bytes.fromhex(NBITS)[::-1]
        + int(nonce, 16).to_bytes(4, "little")
    )


def hash_value(header: bytes) -> int:
    return int.from_bytes(sha256d(header), "little")


@dataclass
class AuditState:
    difficulty: Decimal
    started: float = 0.0
    active: threading.Event = field(default_factory=threading.Event)
    lock: threading.Lock = field(default_factory=threading.Lock)
    submissions: int = 0
    valid: int = 0
    invalid: int = 0
    duplicate: int = 0
    users: set[str] = field(default_factory=set)
    shares: set[tuple[str, str, str, str]] = field(default_factory=set)
    device_rates: list[float] = field(default_factory=list)

    @property
    def target(self) -> int:
        return difficulty_target(self.difficulty)

    def begin(self) -> None:
        with self.lock:
            if self.started == 0.0:
                self.started = time.monotonic()
                self.active.set()

    def record_device_rate(self, rate: float) -> None:
        with self.lock:
            self.device_rates.append(rate)

    def record_submit(
        self, user: str, job_id: str, extranonce2: str, ntime: str, nonce: str
    ) -> tuple[bool, str]:
        self.begin()
        key = (job_id, extranonce2, ntime, nonce)
        try:
            if (
                job_id != JOB_ID
                or len(extranonce2) != EXTRANONCE2_SIZE * 2
                or len(ntime) != 8
                or len(nonce) != 8
            ):
                raise ValueError("malformed share")
            header = build_header(extranonce2, ntime, nonce)
            valid = hash_value(header) <= self.target
        except (ValueError, OverflowError):
            valid = False

        with self.lock:
            self.submissions += 1
            self.users.add(user)
            if key in self.shares:
                self.duplicate += 1
                return False, "duplicate share"
            self.shares.add(key)
            if valid:
                self.valid += 1
                return True, "accepted"
            self.invalid += 1
            return False, "invalid share"

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            elapsed = (
                max(time.monotonic() - self.started, 0.001)
                if self.started
                else 0.001
            )
            valid = self.valid
            work_per_share = (1 << 256) / (self.target + 1)
            inferred = valid * work_per_share / elapsed if valid else 0.0
            relative_error = 1.96 / math.sqrt(valid) if valid else float("inf")
            device_average = (
                sum(self.device_rates) / len(self.device_rates)
                if self.device_rates
                else None
            )
            return {
                "elapsed": elapsed,
                "submissions": self.submissions,
                "valid": valid,
                "invalid": self.invalid,
                "duplicate": self.duplicate,
                "users": sorted(self.users),
                "inferred_hps": inferred,
                "ci_low_hps": max(0.0, inferred * (1.0 - relative_error)),
                "ci_high_hps": inferred * (1.0 + relative_error),
                "device_average_hps": device_average,
                "device_samples": len(self.device_rates),
            }


class AuditServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address: tuple[str, int], state: AuditState):
        self.state = state
        super().__init__(address, AuditHandler)


class AuditHandler(socketserver.StreamRequestHandler):
    server: AuditServer

    def send_json(self, value: dict[str, Any]) -> None:
        self.wfile.write(json.dumps(value, separators=(",", ":")).encode() + b"\n")
        self.wfile.flush()

    def send_job(self) -> None:
        self.server.state.begin()
        self.send_json(
            {
                "id": None,
                "method": "mining.set_difficulty",
                "params": [float(self.server.state.difficulty)],
            }
        )
        self.send_json(
            {
                "id": None,
                "method": "mining.notify",
                "params": [
                    JOB_ID,
                    PREVHASH,
                    COINBASE1,
                    COINBASE2,
                    [],
                    VERSION,
                    NBITS,
                    NTIME,
                    True,
                ],
            }
        )

    def handle(self) -> None:
        peer = f"{self.client_address[0]}:{self.client_address[1]}"
        print(f"client connected: {peer}", flush=True)
        try:
            for raw_line in self.rfile:
                try:
                    request = json.loads(raw_line)
                except json.JSONDecodeError:
                    print(f"invalid JSON from {peer}", flush=True)
                    continue
                request_id = request.get("id")
                method = request.get("method", "")
                params = request.get("params", [])

                if method == "mining.subscribe":
                    self.send_json(
                        {
                            "id": request_id,
                            "result": [
                                [
                                    ["mining.set_difficulty", "audit"],
                                    ["mining.notify", "audit"],
                                ],
                                EXTRANONCE1,
                                EXTRANONCE2_SIZE,
                            ],
                            "error": None,
                        }
                    )
                elif method == "mining.authorize":
                    self.send_json({"id": request_id, "result": True, "error": None})
                    self.send_job()
                elif method in {
                    "mining.suggest_difficulty",
                    "mining.extranonce.subscribe",
                }:
                    self.send_json({"id": request_id, "result": True, "error": None})
                elif method == "mining.submit" and len(params) >= 5:
                    valid, reason = self.server.state.record_submit(
                        str(params[0]),
                        str(params[1]),
                        str(params[2]),
                        str(params[3]),
                        str(params[4]),
                    )
                    self.send_json(
                        {
                            "id": request_id,
                            "result": valid,
                            "error": None if valid else [23, reason, None],
                        }
                    )
                elif request_id is not None:
                    self.send_json({"id": request_id, "result": True, "error": None})
        finally:
            print(f"client disconnected: {peer}", flush=True)


def format_rate(rate: float | None) -> str:
    if rate is None:
        return "n/a"
    if rate >= 1_000_000:
        return f"{rate / 1_000_000:.3f} MH/s"
    if rate >= 1_000:
        return f"{rate / 1_000:.1f} kH/s"
    return f"{rate:.0f} H/s"


def print_snapshot(state: AuditState) -> None:
    snap = state.snapshot()
    print(
        f"elapsed={snap['elapsed']:.0f}s valid={snap['valid']} "
        f"invalid={snap['invalid']} duplicate={snap['duplicate']} "
        f"inferred={format_rate(snap['inferred_hps'])} "
        f"95%CI={format_rate(snap['ci_low_hps'])}..{format_rate(snap['ci_high_hps'])} "
        f"device={format_rate(snap['device_average_hps'])}",
        flush=True,
    )


def poll_device(state: AuditState, url: str, stop: threading.Event) -> None:
    endpoint = url.rstrip("/") + "/probe"
    state.active.wait()
    while not stop.wait(5):
        try:
            with urllib.request.urlopen(endpoint, timeout=3) as response:
                payload = json.load(response)
            rate = float(payload["hr"])
            if rate >= 0:
                state.record_device_rate(rate)
        except (OSError, ValueError, KeyError, json.JSONDecodeError):
            pass


def self_test() -> None:
    test_difficulty = Decimal("0.0000001")
    target = difficulty_target(test_difficulty)
    extranonce2 = "00000000"
    valid_nonce = None
    invalid_nonce = None
    for nonce_value in range(100_000):
        nonce = f"{nonce_value:08x}"
        if hash_value(build_header(extranonce2, NTIME, nonce)) <= target:
            valid_nonce = nonce
        else:
            invalid_nonce = nonce
        if valid_nonce is not None and invalid_nonce is not None:
            state = AuditState(test_difficulty)
            valid, _ = state.record_submit(
                "self-test", JOB_ID, extranonce2, NTIME, valid_nonce
            )
            invalid, _ = state.record_submit(
                "self-test", JOB_ID, extranonce2, NTIME, invalid_nonce
            )
            duplicate, _ = state.record_submit(
                "self-test", JOB_ID, extranonce2, NTIME, valid_nonce
            )
            if not valid or invalid or duplicate:
                raise RuntimeError("share verifier self-test failed")
            print("self-test passed")
            return
    raise RuntimeError("could not find self-test share")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0", help="listen address")
    parser.add_argument("--port", type=int, default=3333, help="listen port")
    parser.add_argument(
        "--difficulty", default="0.0005", help="share difficulty sent to miners"
    )
    parser.add_argument(
        "--duration", type=int, default=600, help="audit duration in seconds"
    )
    parser.add_argument(
        "--device-url", help="optional miner URL whose /probe hr field is sampled"
    )
    parser.add_argument("--self-test", action="store_true", help="run verifier test")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    getcontext().prec = 80
    if args.self_test:
        self_test()
        return 0
    if args.duration <= 0:
        raise SystemExit("--duration must be positive")

    state = AuditState(Decimal(args.difficulty))
    server = AuditServer((args.host, args.port), state)
    stop = threading.Event()
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    if args.device_url:
        threading.Thread(
            target=poll_device, args=(state, args.device_url, stop), daemon=True
        ).start()

    print(
        f"listening on {args.host}:{args.port} difficulty={state.difficulty}; "
        f"configure the miner pool to this host",
        flush=True,
    )
    try:
        while not state.active.wait(1):
            pass
    except KeyboardInterrupt:
        stop.set()
        server.shutdown()
        server.server_close()
        return 0
    print("first audit job issued; measurement started", flush=True)
    deadline = time.monotonic() + args.duration
    try:
        while time.monotonic() < deadline:
            time.sleep(min(30, max(0, deadline - time.monotonic())))
            print_snapshot(state)
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        server.shutdown()
        server.server_close()
    print_snapshot(state)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
