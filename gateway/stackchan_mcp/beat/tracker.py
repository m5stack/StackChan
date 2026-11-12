"""Dependency-free beat tracking over 16 kHz mono PCM chunks."""

from __future__ import annotations

from array import array
from collections import deque
from dataclasses import dataclass
import math
from statistics import median
import sys
import threading


@dataclass(frozen=True)
class BeatSnapshot:
    bpm: float | None
    confidence: float
    last_beat_at: float | None
    onset_count: int


class BeatTracker:
    """Track beat onsets and estimate BPM from a decoded PCM stream."""

    def __init__(
        self,
        *,
        sample_rate: int = 16000,
        bpm_min: float = 60.0,
        bpm_max: float = 200.0,
        onset_window_s: float = 20.0,
        energy_window_s: float = 1.2,
        onset_refractory_s: float = 0.24,
        min_onset_rms: float = 0.004,
        threshold_ratio: float = 1.65,
        rise_ratio: float = 1.18,
    ) -> None:
        self.sample_rate = sample_rate
        self.bpm_min = bpm_min
        self.bpm_max = bpm_max
        self.onset_window_s = onset_window_s
        self.energy_window_s = energy_window_s
        self.onset_refractory_s = onset_refractory_s
        self.min_onset_rms = min_onset_rms
        self.threshold_ratio = threshold_ratio
        self.rise_ratio = rise_ratio

        self._lock = threading.RLock()
        self._energy_history: deque[tuple[float, float]] = deque()
        self._onsets: deque[float] = deque()
        self._last_rms: float | None = None
        self._last_beat_at: float | None = None
        self._bpm: float | None = None
        self._confidence = 0.0
        self._onset_count = 0

    def set_min_onset_rms(self, min_onset_rms: float) -> None:
        """Update the absolute onset floor without resetting beat history."""
        if (
            isinstance(min_onset_rms, bool)
            or not isinstance(min_onset_rms, int | float)
            or not math.isfinite(float(min_onset_rms))
            or float(min_onset_rms) <= 0
        ):
            raise ValueError("min_onset_rms must be a positive finite number")
        with self._lock:
            self.min_onset_rms = float(min_onset_rms)

    def get_min_onset_rms(self) -> float:
        """Return the active absolute onset floor."""
        with self._lock:
            return self.min_onset_rms

    def process_pcm(self, pcm: bytes, *, received_at: float) -> BeatSnapshot:
        """Consume a PCM chunk and return the updated beat snapshot.

        ``pcm`` must be signed 16-bit little-endian mono. ``received_at`` is a
        monotonic timestamp for the end of the chunk; onset timestamps are
        reported near the chunk midpoint.
        """
        if not pcm:
            return self.snapshot()

        usable = len(pcm) - (len(pcm) % 2)
        if usable <= 0:
            return self.snapshot()

        samples = array("h")
        samples.frombytes(pcm[:usable])
        if sys.byteorder != "little":
            samples.byteswap()
        if not samples:
            return self.snapshot()

        rms = math.sqrt(sum(sample * sample for sample in samples) / len(samples))
        rms_norm = min(1.0, rms / 32768.0)
        duration_s = len(samples) / float(self.sample_rate)
        chunk_start = received_at - duration_s
        chunk_at = received_at - duration_s / 2.0
        peak_index = max(range(len(samples)), key=lambda index: abs(samples[index]))
        onset_at = chunk_start + peak_index / float(self.sample_rate)

        with self._lock:
            self._prune_energy(chunk_at)
            baseline = self._energy_baseline()
            threshold = max(self.min_onset_rms, baseline * self.threshold_ratio)
            previous = self._last_rms
            rising = previous is None or rms_norm >= previous * self.rise_ratio
            last_gap_ok = (
                self._last_beat_at is None
                or onset_at - self._last_beat_at >= self.onset_refractory_s
            )

            if rms_norm >= threshold and rising and last_gap_ok:
                self._record_onset(onset_at)

            self._energy_history.append((chunk_at, rms_norm))
            self._last_rms = rms_norm
            self._update_bpm_locked(chunk_at)
            return self._snapshot_locked()

    def snapshot(self) -> BeatSnapshot:
        """Return the latest beat metadata."""
        with self._lock:
            return self._snapshot_locked()

    def _record_onset(self, onset_at: float) -> None:
        self._onsets.append(onset_at)
        self._last_beat_at = onset_at
        self._onset_count += 1

    def _prune_energy(self, now: float) -> None:
        cutoff = now - self.energy_window_s
        while self._energy_history and self._energy_history[0][0] < cutoff:
            self._energy_history.popleft()

    def _prune_onsets(self, now: float) -> None:
        cutoff = now - self.onset_window_s
        while self._onsets and self._onsets[0] < cutoff:
            self._onsets.popleft()

    def _energy_baseline(self) -> float:
        if not self._energy_history:
            return 0.0
        values = [rms for _ts, rms in self._energy_history]
        return sum(values) / len(values)

    def _fold_bpm(self, bpm: float) -> float | None:
        if not math.isfinite(bpm) or bpm <= 0:
            return None
        while bpm < self.bpm_min:
            bpm *= 2.0
        while bpm > self.bpm_max:
            bpm /= 2.0
        if self.bpm_min <= bpm <= self.bpm_max:
            return bpm
        return None

    def _update_bpm_locked(self, now: float) -> None:
        self._prune_onsets(now)
        if len(self._onsets) < 3:
            self._bpm = None
            self._confidence = 0.0
            return

        candidates: list[float] = []
        prev = self._onsets[0]
        for current in list(self._onsets)[1:]:
            interval = current - prev
            prev = current
            if interval <= 0:
                continue
            bpm = self._fold_bpm(60.0 / interval)
            if bpm is not None:
                candidates.append(bpm)

        if len(candidates) < 2:
            self._bpm = None
            self._confidence = 0.0
            return

        bpm = float(median(candidates))
        deviations = [abs(candidate - bpm) for candidate in candidates]
        spread = float(median(deviations)) if deviations else 0.0
        density = min(1.0, len(candidates) / 8.0)
        stability = max(0.0, 1.0 - min(1.0, spread / max(1.0, bpm) * 8.0))
        self._bpm = round(bpm, 2)
        self._confidence = round(density * stability, 3)

    def _snapshot_locked(self) -> BeatSnapshot:
        return BeatSnapshot(
            bpm=self._bpm,
            confidence=self._confidence,
            last_beat_at=self._last_beat_at,
            onset_count=self._onset_count,
        )
