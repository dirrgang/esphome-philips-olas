#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["aioesphomeapi==46.12.0"]
# ///
"""Bounded RF-proxy diagnosis for #23; no firmware or HA changes.

Codec mirrors esphome/include/olas_protocol.h (including its timing windows).
API dependency matches ESPHome 2026.10.1. Importing this module never connects.
"""

import argparse
import asyncio
from collections import Counter
from dataclasses import dataclass
import getpass
import logging
import math
import sys
import time
import warnings


FREQUENCY = 434_000_000
FUNCTIONS = {
    0x10: "off", 0x40: "speed 1", 0xAC: "speed 2", 0x9C: "speed 3",
    0x20: "speed 4", 0x80: "speed 5", 0x8C: "speed 6",
    0x7C: "light on", 0xBC: "light off", 0x6C: "warm white",
    0x64: "neutral white", 0x84: "daylight white", 0x70: "brightness up",
    0x28: "brightness down", 0x50: "reverse", 0x30: "sleep",
    0x1C: "timer 1h", 0x18: "timer 3h", 0x14: "timer 6h",
}


class DiagnosticError(Exception):
    """Safe-to-display diagnostic without credentials or raw captures."""


@dataclass(frozen=True)
class Frame:
    fan_id: int
    command: int
    check: int

    @property
    def function(self):
        return self.command & 0xFC

    @property
    def sequence(self):
        return self.command & 3


def next_sequence(sequence):
    return (sequence + 3) & 3


def checksum(command, check_xor):
    return command ^ check_xor


def encode_frame(fan_id, function, sequence, check_xor):
    if not (0 <= fan_id <= 0xFFFFFF and 0 <= check_xor <= 0xFF
            and function in FUNCTIONS and 0 <= sequence <= 3):
        raise ValueError("Invalid ID, XOR, function or sequence")
    command = function | sequence
    return (fan_id << 17) | (command << 9) | (checksum(command, check_xor) << 1)


def decode_frame(raw, expected_id, check_xor):
    if raw < 0 or raw >= 1 << 41 or raw & 1:
        return None
    fan_id, command, check = raw >> 17, (raw >> 9) & 0xFF, (raw >> 1) & 0xFF
    if (fan_id != expected_id or check != checksum(command, check_xor)
            or command & 0xFC not in FUNCTIONS):
        return None
    return Frame(fan_id, command, check)


def encode_timings(raw):
    if not 0 <= raw < 1 << 41 or raw & 1:
        raise ValueError("Expected a 41-bit frame ending in zero")
    timings = [7400, -1090]
    for bit in range(40, -1, -1):
        timings.extend((730, -340) if raw & (1 << bit) else (340, -730))
    return timings


def decode_timings(timings, expected_id, check_xor):
    """Return ALL valid complete frames; ignore noise and partial tails."""
    frames = []
    for start in range(len(timings) - 83):
        if not (6500 <= timings[start] <= 8200
                and -1450 <= timings[start + 1] <= -750):
            continue
        raw = 0
        for pos in range(start + 2, start + 84, 2):
            mark, space = timings[pos:pos + 2]
            one = 600 <= mark <= 850 and -450 <= space <= -250
            zero = 250 <= mark <= 450 and -900 <= space <= -600
            if not (one or zero):
                break
            raw = (raw << 1) | int(one)
        else:
            if (frame := decode_frame(raw, expected_id, check_xor)) is not None:
                frames.append(frame)
    return frames


def validated_tx_timings(fan_id, sequence, check_xor):
    raw = encode_frame(fan_id, 0xAC, sequence, check_xor)
    timings = encode_timings(raw)
    expected = decode_frame(raw, fan_id, check_xor)
    if (len(timings) != 84
            or any(value == 0 or (value > 0) != (pos % 2 == 0)
                   for pos, value in enumerate(timings))
            or decode_timings(timings, fan_id, check_xor) != [expected]):
        raise DiagnosticError("TX self-check failed: signed timings / MSB-first encoding")
    return timings


def require_terminal():
    if not sys.stdin.isatty() or not sys.stdout.isatty():
        raise DiagnosticError("Interactive terminal required; no key input or RF TX through pipes/CI")


async def terminal_line(prompt, timeout=30):
    """Read without blocking RX callbacks; no worker thread left waiting on timeout."""
    require_terminal()
    print(prompt, end="", flush=True)
    loop = asyncio.get_running_loop()
    result = loop.create_future()

    def ready():
        if not result.done():
            result.set_result(sys.stdin.readline().strip())

    loop.add_reader(sys.stdin.fileno(), ready)
    try:
        return await asyncio.wait_for(result, timeout)
    finally:
        loop.remove_reader(sys.stdin.fileno())


def select_entity(entities, capability, selector, label):
    from aioesphomeapi.model import RadioFrequencyInfo, RadioFrequencyModulation

    candidates = [e for e in entities if isinstance(e, RadioFrequencyInfo)
                  and e.capabilities & capability
                  and e.frequency_min > 0 and e.frequency_max > 0
                  and e.frequency_min <= FREQUENCY <= e.frequency_max
                  and e.supports_modulation(RadioFrequencyModulation.OOK)]
    if selector:
        candidates = [e for e in candidates if f"{e.device_id}:{e.object_id}" == selector]
    if len(candidates) != 1:
        options = ", ".join(f"{e.device_id}:{e.object_id}" for e in candidates) or "none"
        raise DiagnosticError(
            f"{label}: need exactly one advertised RF entity supporting 434 MHz/OOK; "
            f"matches: {options}. Select with --{label} DEVICE_ID:OBJECT_ID. "
            "Missing frequency/modulation metadata is not guessed.")
    entity = candidates[0]
    print(f"{label}: {entity.device_id}:{entity.object_id}, key={entity.key}, "
          f"range={entity.frequency_min}..{entity.frequency_max} Hz, OOK")
    return entity


async def run(args, key):
    from aioesphomeapi import APIClient
    from aioesphomeapi.model import RadioFrequencyCapability, RadioFrequencyModulation

    client = APIClient(args.host, args.port, password="", noise_psk=key)
    stopped = asyncio.Event()
    received = asyncio.Queue(maxsize=64)
    latest = None
    revision = 0
    batches = 0
    valid_count = 0
    seen = {}
    unsubscribe = None

    async def on_stop(expected):
        stopped.set()

    async def bounded(awaitable, timeout):
        work = asyncio.ensure_future(awaitable)
        lost = asyncio.create_task(stopped.wait())
        try:
            done, _ = await asyncio.wait((work, lost), timeout=timeout,
                                         return_when=asyncio.FIRST_COMPLETED)
            if lost in done:
                raise DiagnosticError("API connection closed; no automatic reconnect or TX retry")
            if work not in done:
                raise DiagnosticError("Timeout; no automatic RF transmission/retry")
            return work.result()
        finally:
            for task in (work, lost):
                if not task.done():
                    task.cancel()
            await asyncio.gather(work, lost, return_exceptions=True)

    def on_receive(event):
        nonlocal latest, revision, batches, valid_count
        if (event.device_id, event.key) != (receiver.device_id, receiver.key):
            return
        batches += 1
        frames = decode_timings(event.timings, args.fan_id, args.check_xor)
        valid_count += len(frames)
        now = time.monotonic()
        for frame, count in Counter(frames).items():
            repeated = frame in seen and now - seen[frame] < 0.4
            seen[frame] = now
            print(f"RX: {len(event.timings)} timings, {FUNCTIONS[frame.function]}, "
                  f"seq={frame.sequence}, copies={count}, repeat<400ms={repeated}")
        for frame in frames:
            if frame != latest:
                revision += 1
            latest = frame
        if frames:
            if received.full():
                received.get_nowait()
            received.put_nowait(frames)

    try:
        await bounded(client.connect(on_stop=on_stop, login=True, log_errors=False), args.timeout)
        info = await bounded(client.device_info(), args.timeout)
        print(f"Encrypted API connected; ESPHome {info.esphome_version}")
        entities, _ = await bounded(client.list_entities_services(), args.timeout)
        receiver = select_entity(entities, RadioFrequencyCapability.RECEIVER, args.receiver, "receiver")
        transmitter = None
        if args.tx_speed2:
            transmitter = select_entity(entities, RadioFrequencyCapability.TRANSMITTER,
                                        args.transmitter, "transmitter")
        unsubscribe = client.subscribe_infrared_rf_receive(on_receive)
        if not args.tx_speed2:
            print(f"Receive only for {args.receive_seconds:g}s; press the original remote.")
            await bounded(asyncio.sleep(args.receive_seconds), args.receive_seconds + 1)
            print(f"RX summary: {batches} batches, {valid_count} valid frames; raw data not logged")
            if not valid_count:
                raise DiagnosticError("No matching Olas frame; check remote, ID/XOR, receiver and RX mode")
            return

        print("Controlled TX: press original remote SPEED 1 now. No other RF/HA sender during this test.")
        deadline = time.monotonic() + args.receive_seconds
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise DiagnosticError("No unambiguous speed-1 baseline received before timeout")
            frames = await bounded(received.get(), remaining)
            # Reject mixed batches and stale queued frames.
            if len(set(frames)) == 1 and frames[-1].function == 0x40 and latest == frames[-1]:
                baseline = frames[-1]
                baseline_revision = revision
                break
        sequence = next_sequence(baseline.sequence)
        timings = validated_tx_timings(args.fan_id, sequence, args.check_xor)
        print(f"Prepared speed 2: seq {baseline.sequence} -> {sequence}, 434 MHz ASK/OOK, "
              "84 signed timings/frame, 4 identical frames total (~209 ms).")
        answer = await bounded(terminal_line(
            "Verify suitable 433 MHz antenna and 3.3 V CC1101. "
            "Type SEND to transmit once (30s; anything else cancels): "), 31)
        if answer != "SEND":
            raise DiagnosticError("Cancelled; no RF transmission requested")
        if revision != baseline_revision or latest != baseline:
            raise DiagnosticError("Another Olas command arrived during confirmation; restart for fresh sequence")
        require_terminal()
        # Single synchronous API enqueue; no retry, no replay, no RX->TX callback.
        client.radio_frequency_transmit_raw_timings(
            key=transmitter.key, device_id=transmitter.device_id, frequency=FREQUENCY,
            modulation=RadioFrequencyModulation.OOK, timings=timings, repeat_count=4)
        print("TX request handed to client API. This proves neither RF emission nor fan response. "
              "Physically verify SPEED 2 now; record the observed response separately.")
        # Keep connection open beyond the ~209 ms waveform; not a completion acknowledgement.
        await bounded(asyncio.sleep(1), args.timeout)
        if args.post_rx:
            await bounded(terminal_line(
                "Optional RX return check: press Enter when ready (30s), then press remote SPEED 3: "), 31)
            while not received.empty():
                received.get_nowait()
            deadline = time.monotonic() + args.receive_seconds
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise DiagnosticError("No fresh speed-3 frame after TX; RX return remains unverified")
                frames = await bounded(received.get(), remaining)
                if any(frame.function == 0x9C for frame in frames):
                    print("Fresh remote speed-3 frame received after TX: RX return observed. "
                          "Fan response still requires physical verification.")
                    break
    finally:
        if unsubscribe is not None:
            unsubscribe()
        try:
            await asyncio.wait_for(client.disconnect(), 5)
        except Exception:
            client.force_disconnect()


def number(text):
    return int(text, 0)


def positive_seconds(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError("must be finite and > 0")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", nargs="?", default="192.168.20.157")
    parser.add_argument("--port", type=int, default=6053)
    parser.add_argument("--fan-id", type=number, default=0xAA8C03,
                        help="expected 24-bit Olas ID (default: observed local device)")
    parser.add_argument("--check-xor", type=number, default=0x25,
                        help="device-specific XOR; no automatic derivation from ID")
    parser.add_argument("--receiver", help="advertised DEVICE_ID:OBJECT_ID if ambiguous")
    parser.add_argument("--transmitter", help="advertised DEVICE_ID:OBJECT_ID if ambiguous")
    parser.add_argument("--timeout", type=positive_seconds, default=10, help="API timeout seconds")
    parser.add_argument("--receive-seconds", type=positive_seconds, default=30)
    parser.add_argument("--tx-speed2", action="store_true", help="enable controlled manual speed-2 TX test")
    parser.add_argument("--post-rx", action="store_true", help="after TX, check fresh remote speed-3 RX")
    args = parser.parse_args()
    if not 0 <= args.fan_id <= 0xFFFFFF or not 0 <= args.check_xor <= 0xFF:
        parser.error("fan ID must be 24-bit; check XOR must be 8-bit")
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    if args.post_rx and not args.tx_speed2:
        parser.error("--post-rx requires --tx-speed2")
    # Do not forward third-party debug logs or exception payloads (may contain private data).
    logging.getLogger("aioesphomeapi").setLevel(logging.CRITICAL)
    try:
        require_terminal()
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            key = getpass.getpass("ESPHome API encryption key (not stored): ")
        if not key:
            raise DiagnosticError("Empty encryption key; refusing unencrypted fallback")
        asyncio.run(run(args, key))
    except DiagnosticError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    except (KeyboardInterrupt, EOFError):
        print("Cancelled; no automatic retry.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"Diagnosis failed ({type(exc).__name__}); check API key, IP, port and capabilities. "
              "No automatic TX retry; verify physical state if TX was requested.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
