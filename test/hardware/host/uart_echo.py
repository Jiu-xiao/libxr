#!/usr/bin/env python3
"""Raw serial echo peer for LibXR UART hardware tests.

The adapter RX pin receives DUT TX bytes. This process writes every received byte
back through the adapter TX pin without parsing or framing the payload. The serial
port cannot also be used for logs while this helper is running.

Requires pyserial. Example:
  python uart_echo.py --port /dev/ttyUSB0 --baudrate 115200 --duration-s 120
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import signal
import sys
import threading
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - depends on the operator environment
    raise SystemExit("pyserial is required: python -m pip install pyserial") from exc


@dataclasses.dataclass(frozen=True)
class LineConfig:
    baudrate: int
    bytesize: int = 8
    parity: str = "N"
    stopbits: float = 1.0


@dataclasses.dataclass
class EchoStats:
    port: str
    config: LineConfig
    rx_bytes: int = 0
    tx_bytes: int = 0
    read_calls: int = 0
    write_calls: int = 0
    partial_writes: int = 0
    elapsed_s: float = 0.0
    exit_reason: str = "running"
    error: str | None = None


class SerialEchoTransport:
    def __init__(self, chunk_size: int = 4096) -> None:
        if chunk_size <= 0:
            raise ValueError("chunk_size must be positive")
        self._chunk_size = chunk_size
        self._serial: serial.Serial | None = None
        self._port_name = ""
        self._config = LineConfig(baudrate=115200)

    def open(self, port: str, config: LineConfig) -> None:
        if self._serial is not None:
            raise RuntimeError("serial transport is already open")
        self._serial = serial.Serial(
            port=port,
            baudrate=config.baudrate,
            bytesize=config.bytesize,
            parity=config.parity,
            stopbits=config.stopbits,
            timeout=0.02,
            write_timeout=1.0,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        self._serial.dtr = False
        self._serial.rts = False
        self._serial.reset_input_buffer()
        self._serial.reset_output_buffer()
        self._port_name = port
        self._config = config

    def reconfigure(self, config: LineConfig) -> None:
        if self._serial is None:
            raise RuntimeError("serial transport is not open")
        self._serial.baudrate = config.baudrate
        self._serial.bytesize = config.bytesize
        self._serial.parity = config.parity
        self._serial.stopbits = config.stopbits
        self._config = config

    def close(self) -> None:
        if self._serial is not None:
            self._serial.close()
            self._serial = None

    def pump(self, stop_event: threading.Event, duration_s: float = 0.0) -> EchoStats:
        if self._serial is None:
            raise RuntimeError("serial transport is not open")

        stats = EchoStats(port=self._port_name, config=self._config)
        started = time.monotonic()
        deadline = started + duration_s if duration_s > 0.0 else None
        try:
            while not stop_event.is_set():
                if deadline is not None and time.monotonic() >= deadline:
                    stats.exit_reason = "duration"
                    break

                available = self._serial.in_waiting
                data = self._serial.read(
                    min(self._chunk_size, available) if available > 0 else 1
                )
                stats.read_calls += 1
                if not data:
                    continue

                stats.rx_bytes += len(data)
                view = memoryview(data)
                offset = 0
                while offset < len(view):
                    written = self._serial.write(view[offset:])
                    stats.write_calls += 1
                    if written <= 0:
                        raise serial.SerialTimeoutException("serial write made no progress")
                    if written < len(view) - offset:
                        stats.partial_writes += 1
                    offset += written
                    stats.tx_bytes += written
            else:
                stats.exit_reason = "signal"
        except Exception as exc:  # capture the final machine-readable failure
            stats.exit_reason = "error"
            stats.error = f"{type(exc).__name__}: {exc}"
        finally:
            stats.elapsed_s = time.monotonic() - started
        return stats


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baudrate", required=True, type=int)
    parser.add_argument("--bytesize", type=int, choices=(5, 6, 7, 8), default=8)
    parser.add_argument("--parity", choices=("N", "E", "O"), default="N")
    parser.add_argument("--stopbits", type=float, choices=(1.0, 1.5, 2.0), default=1.0)
    parser.add_argument("--chunk-size", type=int, default=4096)
    parser.add_argument("--duration-s", type=float, default=0.0)
    parser.add_argument("--stats-json", type=Path)
    return parser.parse_args()


def emit(event: str, payload: object) -> None:
    print(json.dumps({"event": event, "data": payload}, separators=(",", ":")), flush=True)


def main() -> int:
    args = parse_args()
    config = LineConfig(
        baudrate=args.baudrate,
        bytesize=args.bytesize,
        parity=args.parity,
        stopbits=args.stopbits,
    )
    stop_event = threading.Event()

    def request_stop(_signum: int, _frame: object) -> None:
        stop_event.set()

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    transport = SerialEchoTransport(chunk_size=args.chunk_size)
    try:
        transport.open(args.port, config)
        emit("ready", {"port": args.port, "config": dataclasses.asdict(config)})
        stats = transport.pump(stop_event, duration_s=args.duration_s)
    except Exception as exc:
        stats = EchoStats(
            port=args.port,
            config=config,
            exit_reason="open_error",
            error=f"{type(exc).__name__}: {exc}",
        )
    finally:
        transport.close()

    payload = dataclasses.asdict(stats)
    emit("final", payload)
    if args.stats_json is not None:
        args.stats_json.parent.mkdir(parents=True, exist_ok=True)
        args.stats_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0 if stats.error is None else 1


if __name__ == "__main__":
    sys.exit(main())
