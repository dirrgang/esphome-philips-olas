"""Synthetic host fixtures only. Every API client in these tests is a fake."""

import asyncio
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock, patch

SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "rf_proxy_diagnose.py"
spec = importlib.util.spec_from_file_location("rf_proxy_diagnose", SCRIPT)
rf = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = rf
spec.loader.exec_module(rf)

ID, XOR = 0xAA8C03, 0x25


class CodecTests(unittest.TestCase):
    def test_cpp_reference_vector_and_bit_order(self):
        # Same fixed vector as tests/test_protocol.cpp, not a round-trip alone.
        raw = rf.encode_frame(ID, 0x40, 0, XOR)
        self.assertEqual(raw, 0x155180680CA)
        self.assertEqual(rf.checksum(0x40, XOR), 0x65)
        timings = rf.encode_timings(raw)
        self.assertEqual(timings[:8], [7400, -1090, 730, -340, 340, -730, 730, -340])
        self.assertEqual(timings[-2:], [340, -730])
        self.assertEqual(len(timings), 84)

    def test_all_functions_sequences_and_device_specific_checks(self):
        for fan_id, check in ((ID, XOR), (0x123456, 0x5B)):
            for function in rf.FUNCTIONS:
                for sequence in range(4):
                    with self.subTest(fan_id=fan_id, function=function, sequence=sequence):
                        raw = rf.encode_frame(fan_id, function, sequence, check)
                        frame = rf.decode_frame(raw, fan_id, check)
                        self.assertEqual(frame.function, function)
                        self.assertEqual(frame.sequence, sequence)
                        self.assertEqual(frame.fan_id, fan_id)
                        self.assertEqual(rf.decode_timings(rf.encode_timings(raw), fan_id, check), [frame])
                        self.assertIsNone(rf.decode_frame(raw, fan_id ^ 1, check))
                        self.assertIsNone(rf.decode_frame(raw, fan_id, check ^ 1))

    def test_invalid_frames_and_unknown_function(self):
        raw = rf.encode_frame(ID, 0x40, 0, XOR)
        for invalid in (raw | 1, raw ^ 2, 1 << 41, -1):
            self.assertIsNone(rf.decode_frame(invalid, ID, XOR))
        unknown = (ID << 17) | (0xFC << 9) | ((0xFC ^ XOR) << 1)
        self.assertIsNone(rf.decode_frame(unknown, ID, XOR))
        for values in ((1 << 24, 0x40, 0, XOR), (ID, 0x41, 0, XOR),
                       (ID, 0x40, 4, XOR), (ID, 0x40, 0, 256)):
            with self.assertRaises(ValueError):
                rf.encode_frame(*values)

    def test_wraparound(self):
        self.assertEqual([rf.next_sequence(i) for i in range(4)], [3, 0, 1, 2])
        sequence = 1
        observed = [sequence]
        for _ in range(4):
            sequence = rf.next_sequence(sequence)
            observed.append(sequence)
        self.assertEqual(observed, [1, 0, 3, 2, 1])

    def test_repeats_noise_partial_tail_and_mixed_commands(self):
        raw = rf.encode_frame(ID, 0x40, 1, XOR)
        wave = rf.encode_timings(raw)
        # 385 synthetic values, like the observed RMT batch length, not a capture.
        timings = [77, -90] + wave * 4 + wave[:47]
        self.assertEqual(len(timings), 385)
        frame = rf.decode_frame(raw, ID, XOR)
        self.assertEqual(rf.decode_timings(timings, ID, XOR), [frame] * 4)
        other = rf.encode_frame(ID, 0xAC, 0, XOR)
        self.assertEqual(rf.decode_timings(wave + rf.encode_timings(other), ID, XOR),
                         [frame, rf.decode_frame(other, ID, XOR)])
        self.assertEqual(rf.decode_timings(wave[:-1], ID, XOR), [])
        self.assertEqual(rf.decode_timings([], ID, XOR), [])

    def test_timing_windows_and_signed_format(self):
        wave = rf.validated_tx_timings(ID, 3, XOR)
        self.assertTrue(all((v > 0) == (i % 2 == 0) for i, v in enumerate(wave)))
        self.assertEqual(rf.decode_timings([abs(v) for v in wave], ID, XOR), [])
        for pos, invalid in ((0, 6499), (1, -1451), (2, 900), (3, 340)):
            broken = wave.copy()
            broken[pos] = invalid
            self.assertEqual(rf.decode_timings(broken, ID, XOR), [])
        with patch.object(rf, "encode_timings", return_value=[1, 2]):
            with self.assertRaises(rf.DiagnosticError):
                rf.validated_tx_timings(ID, 0, XOR)

    def test_no_terminal_aborts(self):
        with patch.object(sys, "stdin") as stdin:
            stdin.isatty.return_value = False
            with self.assertRaises(rf.DiagnosticError):
                rf.require_terminal()


class FakeRFInfo(SimpleNamespace):
    def supports_modulation(self, modulation):
        return bool(self.supported_modulations & (1 << modulation))


def entity(**overrides):
    values = dict(device_id=7, object_id="proxy", key=123, capabilities=3,
                  frequency_min=rf.FREQUENCY, frequency_max=rf.FREQUENCY,
                  supported_modulations=1)
    return FakeRFInfo(**(values | overrides))


class ProxyTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.client = Mock()
        self.client.connect = AsyncMock()
        self.client.disconnect = AsyncMock()
        self.client.device_info = AsyncMock(return_value=SimpleNamespace(esphome_version="2026.10.1"))
        self.client.list_entities_services = AsyncMock(return_value=([entity()], []))
        self.client.subscribe_infrared_rf_receive.side_effect = self.subscribe
        self.callback = None
        api = SimpleNamespace(APIClient=Mock(return_value=self.client))
        model = SimpleNamespace(RadioFrequencyInfo=FakeRFInfo,
                                RadioFrequencyCapability=SimpleNamespace(RECEIVER=2, TRANSMITTER=1),
                                RadioFrequencyModulation=SimpleNamespace(OOK=0))
        self.modules = patch.dict(sys.modules, {"aioesphomeapi": api, "aioesphomeapi.model": model})
        self.modules.start()
        self.output = patch("builtins.print")
        self.output.start()
        self.addCleanup(self.modules.stop)
        self.addCleanup(self.output.stop)
        self.args = SimpleNamespace(host="test.invalid", port=6053, timeout=0.05,
                                    receive_seconds=0.02, fan_id=ID, check_xor=XOR,
                                    receiver=None, transmitter=None, tx_speed2=False, post_rx=False)

    def emit(self, function=0x40, sequence=0, device_id=7, key=123):
        self.callback(SimpleNamespace(device_id=device_id, key=key,
                                      timings=rf.encode_timings(rf.encode_frame(ID, function, sequence, XOR)) * 4))

    def subscribe(self, callback):
        self.callback = callback
        asyncio.get_running_loop().call_soon(self.emit)
        return Mock()

    async def test_receive_only_never_calls_tx(self):
        await rf.run(self.args, "fake-key")
        self.client.radio_frequency_transmit_raw_timings.assert_not_called()
        self.client.disconnect.assert_awaited_once()

    async def test_discovery_rejects_unknown_frequency_modulation_and_ambiguity(self):
        for entities in ([entity(frequency_min=0)], [entity(supported_modulations=0)],
                         [entity(), entity(key=124)], [entity(capabilities=0)]):
            with self.subTest(entities=entities), self.assertRaises(rf.DiagnosticError):
                rf.select_entity(entities, 1, None, "transmitter")
        selected = rf.select_entity([entity(), entity(object_id="other")], 1, "7:other", "transmitter")
        self.assertEqual(selected.object_id, "other")

    async def test_wrong_event_source_times_out_and_closes(self):
        self.client.subscribe_infrared_rf_receive.side_effect = lambda cb: (
            setattr(self, "callback", cb), self.emit(device_id=8), Mock())[-1]
        with self.assertRaises(rf.DiagnosticError):
            await rf.run(self.args, "fake-key")
        self.client.radio_frequency_transmit_raw_timings.assert_not_called()
        self.client.disconnect.assert_awaited_once()

    async def test_confirmation_cancel_and_changed_baseline_prevent_tx(self):
        self.args.tx_speed2 = True
        for changed in (False, True):
            async def confirm(*args):
                if changed:
                    self.emit(0x9C, 3)
                return "SEND" if changed else "no"
            with patch.object(rf, "terminal_line", side_effect=confirm):
                with self.assertRaises(rf.DiagnosticError):
                    await rf.run(self.args, "fake-key")
            self.client.radio_frequency_transmit_raw_timings.assert_not_called()

    async def test_mock_tx_has_discovered_identity_and_four_total_frames(self):
        self.args.tx_speed2 = True
        self.args.timeout = 2
        with patch.object(rf, "terminal_line", new=AsyncMock(return_value="SEND")), \
                patch.object(rf, "require_terminal"):
            await rf.run(self.args, "fake-key")
        kwargs = self.client.radio_frequency_transmit_raw_timings.call_args.kwargs
        self.assertEqual({k: kwargs[k] for k in ("key", "device_id", "frequency", "modulation", "repeat_count")},
                         dict(key=123, device_id=7, frequency=434_000_000, modulation=0, repeat_count=4))
        self.assertEqual(len(kwargs["timings"]), 84)
        decoded = rf.decode_timings(kwargs["timings"], ID, XOR)
        self.assertEqual((decoded[0].function, decoded[0].sequence), (0xAC, 3))
        self.client.radio_frequency_transmit_raw_timings.assert_called_once()

    async def test_connection_failure_closes(self):
        self.client.connect.side_effect = OSError("synthetic")
        with self.assertRaises(OSError):
            await rf.run(self.args, "fake-key")
        self.client.disconnect.assert_awaited_once()
        self.client.radio_frequency_transmit_raw_timings.assert_not_called()

    async def test_no_speed1_baseline_or_lost_connection_never_transmits(self):
        self.args.tx_speed2 = True
        self.client.subscribe_infrared_rf_receive.side_effect = lambda cb: Mock()
        with self.assertRaises(rf.DiagnosticError):
            await rf.run(self.args, "fake-key")
        self.client.radio_frequency_transmit_raw_timings.assert_not_called()

        async def connect(**kwargs):
            asyncio.get_running_loop().call_soon(
                lambda: asyncio.create_task(kwargs["on_stop"](False)))

        self.client.connect.side_effect = connect
        with self.assertRaises(rf.DiagnosticError):
            await rf.run(self.args, "fake-key")
        self.client.radio_frequency_transmit_raw_timings.assert_not_called()

    async def test_terminal_guard_remains_immediately_before_tx(self):
        self.args.tx_speed2 = True
        with patch.object(rf, "terminal_line", new=AsyncMock(return_value="SEND")), \
                patch.object(sys, "stdin") as stdin:
            stdin.isatty.return_value = False
            with self.assertRaises(rf.DiagnosticError):
                await rf.run(self.args, "fake-key")
        self.client.radio_frequency_transmit_raw_timings.assert_not_called()

    async def test_post_rx_requires_fresh_remote_speed3(self):
        self.args.tx_speed2 = True
        self.args.post_rx = True
        self.args.timeout = 2

        async def prompt(text):
            if "Optional RX" in text:
                asyncio.get_running_loop().call_later(0.001, self.emit, 0x9C, 2)
                return ""
            return "SEND"

        with patch.object(rf, "terminal_line", side_effect=prompt), \
                patch.object(rf, "require_terminal"):
            await rf.run(self.args, "fake-key")
        self.client.radio_frequency_transmit_raw_timings.assert_called_once()
        self.client.disconnect.assert_awaited_once()


if __name__ == "__main__":
    unittest.main()
