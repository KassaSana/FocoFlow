"""
Feature extraction for Neural Focus event streams.
"""

from __future__ import annotations

from collections import deque
import csv
from dataclasses import dataclass
from datetime import datetime
import math
import os
import struct
from typing import Deque, Iterable, List, Optional, Tuple

from .event_schema import EventRecord, EventType

MOUSE_MOVE_STRUCT = struct.Struct("<iiI")
IDLE_STRUCT = struct.Struct("<I")

BROWSERS = {
    "chrome.exe",
    "msedge.exe",
    "firefox.exe",
    "brave.exe",
    "opera.exe",
}
IDES = {
    "code.exe",
    "devenv.exe",
    "idea64.exe",
    "pycharm64.exe",
    "clion64.exe",
    "rider64.exe",
}
COMMUNICATION = {
    "slack.exe",
    "discord.exe",
    "teams.exe",
    "outlook.exe",
    "zoom.exe",
}
ENTERTAINMENT = {
    "spotify.exe",
    "steam.exe",
    "vlc.exe",
}
PRODUCTIVITY = {
    "winword.exe",
    "excel.exe",
    "powerpnt.exe",
    "notion.exe",
    "obsidian.exe",
}


@dataclass(frozen=True)
class FeatureVector:
    timestamp: float
    seconds_since_session_start: int
    hour_of_day: int
    day_of_week: int
    minutes_since_last_break: int
    keystroke_count: int
    keystroke_rate: float
    keystroke_interval_mean: float
    keystroke_interval_std: float
    keystroke_interval_trend: float
    mouse_move_count: int
    mouse_distance_pixels: float
    mouse_speed_mean: float
    mouse_speed_std: float
    mouse_acceleration_mean: float
    mouse_click_count: int
    context_switches_30s: int
    context_switches_5min: int
    time_in_current_app: int
    unique_apps_5min: int
    idle_time_30s: float
    idle_event_count_5min: int
    longest_active_stretch_5min: int
    window_title_length: int
    window_title_changed_30s: bool
    is_browser: bool
    is_ide: bool
    is_communication: bool
    is_entertainment: bool
    is_productivity: bool
    focus_momentum: float
    productivity_category: str
    is_pseudo_productive: bool
    recent_event_sequence: Optional[List[int]]

    @staticmethod
    def headers() -> List[str]:
        return [
            "timestamp",
            "seconds_since_session_start",
            "hour_of_day",
            "day_of_week",
            "minutes_since_last_break",
            "keystroke_count",
            "keystroke_rate",
            "keystroke_interval_mean",
            "keystroke_interval_std",
            "keystroke_interval_trend",
            "mouse_move_count",
            "mouse_distance_pixels",
            "mouse_speed_mean",
            "mouse_speed_std",
            "mouse_acceleration_mean",
            "mouse_click_count",
            "context_switches_30s",
            "context_switches_5min",
            "time_in_current_app",
            "unique_apps_5min",
            "idle_time_30s",
            "idle_event_count_5min",
            "longest_active_stretch_5min",
            "window_title_length",
            "window_title_changed_30s",
            "is_browser",
            "is_ide",
            "is_communication",
            "is_entertainment",
            "is_productivity",
            "focus_momentum",
            "productivity_category",
            "is_pseudo_productive",
            "recent_event_sequence",
        ]

    def to_row(self) -> List[str]:
        sequence = ""
        if self.recent_event_sequence:
            sequence = ",".join(str(x) for x in self.recent_event_sequence)
        return [
            f"{self.timestamp:.6f}",
            str(self.seconds_since_session_start),
            str(self.hour_of_day),
            str(self.day_of_week),
            str(self.minutes_since_last_break),
            str(self.keystroke_count),
            f"{self.keystroke_rate:.4f}",
            f"{self.keystroke_interval_mean:.6f}",
            f"{self.keystroke_interval_std:.6f}",
            f"{self.keystroke_interval_trend:.6f}",
            str(self.mouse_move_count),
            f"{self.mouse_distance_pixels:.2f}",
            f"{self.mouse_speed_mean:.2f}",
            f"{self.mouse_speed_std:.2f}",
            f"{self.mouse_acceleration_mean:.2f}",
            str(self.mouse_click_count),
            str(self.context_switches_30s),
            str(self.context_switches_5min),
            str(self.time_in_current_app),
            str(self.unique_apps_5min),
            f"{self.idle_time_30s:.2f}",
            str(self.idle_event_count_5min),
            str(self.longest_active_stretch_5min),
            str(self.window_title_length),
            str(int(self.window_title_changed_30s)),
            str(int(self.is_browser)),
            str(int(self.is_ide)),
            str(int(self.is_communication)),
            str(int(self.is_entertainment)),
            str(int(self.is_productivity)),
            f"{self.focus_momentum:.2f}",
            self.productivity_category,
            str(int(self.is_pseudo_productive)),
            sequence,
        ]


def write_features_csv(path: str, features: Iterable[FeatureVector]) -> int:
    count = 0
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(FeatureVector.headers())
        for feature in features:
            writer.writerow(feature.to_row())
            count += 1
    return count


def append_feature_csv(path: str, feature: FeatureVector) -> None:
    needs_header = not os.path.exists(path) or os.path.getsize(path) == 0
    with open(path, "a", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        if needs_header:
            writer.writerow(FeatureVector.headers())
        writer.writerow(feature.to_row())


def _mean(values: Iterable[float]) -> float:
    values = list(values)
    if not values:
        return 0.0
    return sum(values) / len(values)


def _std(values: Iterable[float]) -> float:
    values = list(values)
    if len(values) < 2:
        return 0.0
    avg = _mean(values)
    variance = sum((v - avg) ** 2 for v in values) / (len(values) - 1)
    return math.sqrt(variance)


def _linear_slope(values: List[float]) -> float:
    if len(values) < 2:
        return 0.0
    xs = list(range(len(values)))
    mean_x = _mean(xs)
    mean_y = _mean(values)
    num = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, values))
    den = sum((x - mean_x) ** 2 for x in xs)
    if den == 0:
        return 0.0
    return num / den


def _event_time(event: EventRecord) -> float:
    return event.timestamp_us / 1_000_000.0


def _parse_mouse_move(event: EventRecord) -> Tuple[int, int, int]:
    x, y, speed = MOUSE_MOVE_STRUCT.unpack_from(event.data_raw)
    return x, y, speed


def _idle_duration_ms(event: EventRecord) -> int:
    return IDLE_STRUCT.unpack_from(event.data_raw)[0]


def _classify_app(app_name: str) -> Tuple[bool, bool, bool, bool, bool]:
    name = app_name.lower()
    return (
        name in BROWSERS,
        name in IDES,
        name in COMMUNICATION,
        name in ENTERTAINMENT,
        name in PRODUCTIVITY,
    )


class FeatureExtractor:
    def __init__(
        self,
        window_seconds: int = 30,
        long_window_seconds: int = 300,
        break_threshold_seconds: int = 300,
    ) -> None:
        self.window_seconds = window_seconds
        self.long_window_seconds = long_window_seconds
        self.break_threshold_seconds = break_threshold_seconds

        self.events_30s: Deque[EventRecord] = deque()
        self.events_5min: Deque[EventRecord] = deque()
        self.recent_event_types: Deque[int] = deque(maxlen=10)

        self.session_start_ts: Optional[float] = None
        self.last_break_ts: Optional[float] = None
        self.current_app_name: str = ""
        self.current_app_start_ts: Optional[float] = None
        self.focus_momentum: float = 0.0

    def update_focus_score(self, score: float, alpha: float = 0.2) -> None:
        self.focus_momentum = alpha * score + (1 - alpha) * self.focus_momentum

    def update(self, event: EventRecord) -> FeatureVector:
        now = _event_time(event)
        if self.session_start_ts is None:
            self.session_start_ts = now
            self.last_break_ts = now
            self.current_app_name = event.app_name
            self.current_app_start_ts = now

        self.events_30s.append(event)
        self.events_5min.append(event)
        self.recent_event_types.append(int(event.event_type))

        self._trim(now)
        self._update_break_state(event, now)
        self._update_current_app(event, now)

        return self.extract_features(now)

    def extract_features(self, now: Optional[float] = None) -> FeatureVector:
        if not self.events_5min:
            timestamp = now or datetime.utcnow().timestamp()
            return FeatureVector(
                timestamp=timestamp,
                seconds_since_session_start=0,
                hour_of_day=datetime.utcnow().hour,
                day_of_week=datetime.utcnow().weekday(),
                minutes_since_last_break=0,
                keystroke_count=0,
                keystroke_rate=0.0,
                keystroke_interval_mean=0.0,
                keystroke_interval_std=0.0,
                keystroke_interval_trend=0.0,
                mouse_move_count=0,
                mouse_distance_pixels=0.0,
                mouse_speed_mean=0.0,
                mouse_speed_std=0.0,
                mouse_acceleration_mean=0.0,
                mouse_click_count=0,
                context_switches_30s=0,
                context_switches_5min=0,
                time_in_current_app=0,
                unique_apps_5min=0,
                idle_time_30s=0.0,
                idle_event_count_5min=0,
                longest_active_stretch_5min=0,
                window_title_length=0,
                window_title_changed_30s=False,
                is_browser=False,
                is_ide=False,
                is_communication=False,
                is_entertainment=False,
                is_productivity=False,
                focus_momentum=self.focus_momentum,
                productivity_category="Unknown",
                is_pseudo_productive=False,
                recent_event_sequence=list(self.recent_event_types),
            )

        now_ts = now or _event_time(self.events_5min[-1])
        oldest_30s = _event_time(self.events_30s[0]) if self.events_30s else now_ts
        oldest_5min = _event_time(self.events_5min[0]) if self.events_5min else now_ts
        span_30s = max(1e-6, min(self.window_seconds, now_ts - oldest_30s))
        span_5min = max(1e-6, min(self.long_window_seconds, now_ts - oldest_5min))

        keystrokes = [e for e in self.events_30s if e.event_type == EventType.KEY_PRESS]
        key_times = [_event_time(e) for e in keystrokes]
        intervals = [key_times[i] - key_times[i - 1] for i in range(1, len(key_times))]
        keystroke_interval_mean = _mean(intervals)
        keystroke_interval_std = _std(intervals)
        keystroke_interval_trend = _linear_slope(intervals)

        mouse_moves = [e for e in self.events_30s if e.event_type == EventType.MOUSE_MOVE]
        mouse_clicks = [e for e in self.events_30s if e.event_type == EventType.MOUSE_CLICK]
        speeds = []
        distances = []
        accelerations = []
        for i, event in enumerate(mouse_moves):
            x, y, speed = _parse_mouse_move(event)
            speeds.append(speed)
            if i > 0:
                prev_ts = _event_time(mouse_moves[i - 1])
                dt = max(1e-6, _event_time(event) - prev_ts)
                distances.append(speed * dt)
                prev_speed = speeds[i - 1]
                accelerations.append(abs(speed - prev_speed) / dt)
        mouse_speed_mean = _mean(speeds)
        mouse_speed_std = _std(speeds)
        mouse_distance_pixels = sum(distances)
        mouse_acceleration_mean = _mean(accelerations)

        context_switches_30s = sum(
            1 for e in self.events_30s if e.event_type == EventType.WINDOW_FOCUS_CHANGE
        )
        context_switches_5min = sum(
            1 for e in self.events_5min if e.event_type == EventType.WINDOW_FOCUS_CHANGE
        )
        unique_apps_5min = len({e.app_name for e in self.events_5min if e.app_name})
        time_in_current_app = 0
        if self.current_app_start_ts is not None:
            time_in_current_app = int(now_ts - self.current_app_start_ts)

        idle_events_30s = [
            e
            for e in self.events_30s
            if e.event_type in (EventType.IDLE_START, EventType.IDLE_END)
        ]
        idle_time_30s = sum(_idle_duration_ms(e) for e in idle_events_30s) / 1000.0

        idle_events_5min = [
            e
            for e in self.events_5min
            if e.event_type in (EventType.IDLE_START, EventType.IDLE_END)
        ]
        idle_event_count_5min = len(idle_events_5min)
        idle_timestamps = sorted(_event_time(e) for e in idle_events_5min)
        if not idle_timestamps:
            longest_active_stretch_5min = int(min(self.long_window_seconds, span_5min))
        else:
            window_start = now_ts - self.long_window_seconds
            boundaries = [window_start] + idle_timestamps + [now_ts]
            longest_active_stretch_5min = int(
                max(b - a for a, b in zip(boundaries, boundaries[1:]))
            )

        window_title_changed_30s = any(
            e.event_type == EventType.WINDOW_TITLE_CHANGE for e in self.events_30s
        )
        window_title_length = 0

        is_browser, is_ide, is_comm, is_ent, is_prod = _classify_app(self.current_app_name)
        productivity_category = "Unknown"
        if is_ide:
            productivity_category = "Building"
        elif is_prod:
            productivity_category = "Writing"
        elif is_browser:
            productivity_category = "Browsing"
        elif is_comm:
            productivity_category = "Communicating"
        elif is_ent:
            productivity_category = "Entertainment"

        is_pseudo_productive = False

        minutes_since_last_break = 0
        if self.last_break_ts is not None:
            minutes_since_last_break = int(max(0.0, (now_ts - self.last_break_ts) / 60.0))

        dt = datetime.fromtimestamp(now_ts)
        return FeatureVector(
            timestamp=now_ts,
            seconds_since_session_start=int(now_ts - (self.session_start_ts or now_ts)),
            hour_of_day=dt.hour,
            day_of_week=dt.weekday(),
            minutes_since_last_break=minutes_since_last_break,
            keystroke_count=len(keystrokes),
            keystroke_rate=len(keystrokes) / span_30s,
            keystroke_interval_mean=keystroke_interval_mean,
            keystroke_interval_std=keystroke_interval_std,
            keystroke_interval_trend=keystroke_interval_trend,
            mouse_move_count=len(mouse_moves),
            mouse_distance_pixels=mouse_distance_pixels,
            mouse_speed_mean=mouse_speed_mean,
            mouse_speed_std=mouse_speed_std,
            mouse_acceleration_mean=mouse_acceleration_mean,
            mouse_click_count=len(mouse_clicks),
            context_switches_30s=context_switches_30s,
            context_switches_5min=context_switches_5min,
            time_in_current_app=time_in_current_app,
            unique_apps_5min=unique_apps_5min,
            idle_time_30s=idle_time_30s,
            idle_event_count_5min=idle_event_count_5min,
            longest_active_stretch_5min=longest_active_stretch_5min,
            window_title_length=window_title_length,
            window_title_changed_30s=window_title_changed_30s,
            is_browser=is_browser,
            is_ide=is_ide,
            is_communication=is_comm,
            is_entertainment=is_ent,
            is_productivity=is_prod,
            focus_momentum=self.focus_momentum,
            productivity_category=productivity_category,
            is_pseudo_productive=is_pseudo_productive,
            recent_event_sequence=list(self.recent_event_types),
        )

    def _trim(self, now: float) -> None:
        while self.events_30s and now - _event_time(self.events_30s[0]) > self.window_seconds:
            self.events_30s.popleft()
        while self.events_5min and now - _event_time(self.events_5min[0]) > self.long_window_seconds:
            self.events_5min.popleft()

    def _update_break_state(self, event: EventRecord, now: float) -> None:
        if event.event_type in (EventType.IDLE_START, EventType.IDLE_END):
            duration = _idle_duration_ms(event) / 1000.0
            if duration >= self.break_threshold_seconds:
                self.last_break_ts = now

    def _update_current_app(self, event: EventRecord, now: float) -> None:
        if event.event_type == EventType.WINDOW_FOCUS_CHANGE:
            self.current_app_name = event.app_name
            self.current_app_start_ts = now
