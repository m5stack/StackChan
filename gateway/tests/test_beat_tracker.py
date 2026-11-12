from __future__ import annotations

import pytest

from stackchan_mcp.beat.tracker import BeatTracker
from stackchan_mcp.stt.audio_utils import DEVICE_SAMPLE_RATE


def _pcm_chunk(
    *,
    start_s: float,
    duration_s: float,
    bpm: float,
    sample_rate: int = DEVICE_SAMPLE_RATE,
) -> bytes:
    samples = int(duration_s * sample_rate)
    beat_period = 60.0 / bpm
    out = bytearray()
    for index in range(samples):
        t = start_s + index / sample_rate
        nearest_beat = round(t / beat_period) * beat_period
        if abs(t - nearest_beat) <= 0.012:
            value = 18000
        else:
            value = 220 if index % 17 < 8 else -220
        out.extend(int(value).to_bytes(2, "little", signed=True))
    return bytes(out)


def test_tracker_estimates_bpm_from_synthetic_click_track() -> None:
    tracker = BeatTracker(sample_rate=DEVICE_SAMPLE_RATE)
    chunk_s = 0.06
    elapsed = 0.0

    for _ in range(int(12.0 / chunk_s)):
        pcm = _pcm_chunk(start_s=elapsed, duration_s=chunk_s, bpm=120.0)
        elapsed += chunk_s
        tracker.process_pcm(pcm, received_at=elapsed)

    snapshot = tracker.snapshot()
    assert snapshot.bpm is not None
    assert abs(snapshot.bpm - 120.0) <= 3.0
    assert snapshot.confidence >= 0.5
    assert snapshot.onset_count >= 16


def test_tracker_stays_unconfident_without_onsets() -> None:
    tracker = BeatTracker(sample_rate=DEVICE_SAMPLE_RATE)
    chunk = (120).to_bytes(2, "little", signed=True) * int(0.06 * DEVICE_SAMPLE_RATE)
    elapsed = 0.0

    for _ in range(20):
        elapsed += 0.06
        tracker.process_pcm(chunk, received_at=elapsed)

    snapshot = tracker.snapshot()
    assert snapshot.bpm is None
    assert snapshot.confidence == 0.0


def test_min_onset_rms_setter_preserves_tracker_history() -> None:
    tracker = BeatTracker(sample_rate=DEVICE_SAMPLE_RATE)
    with tracker._lock:
        tracker._record_onset(1.0)
        tracker._record_onset(1.5)
        tracker._record_onset(2.0)
        tracker._update_bpm_locked(2.0)
    before = tracker.snapshot()

    tracker.set_min_onset_rms(0.001)

    assert tracker.get_min_onset_rms() == pytest.approx(0.001)
    assert tracker.snapshot() == before
