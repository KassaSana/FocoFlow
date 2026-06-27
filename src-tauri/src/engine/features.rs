use std::collections::VecDeque;

use chrono::{Datelike, Timelike, Utc};

use crate::types::{CaptureEvent, EventType};

#[derive(Debug, Clone)]
pub struct FeatureVector {
    pub timestamp: f64,
    pub seconds_since_session_start: i64,
    pub hour_of_day: u32,
    pub day_of_week: u32,
    pub minutes_since_last_break: i64,
    pub keystroke_count: usize,
    pub keystroke_rate: f64,
    pub keystroke_interval_mean: f64,
    pub keystroke_interval_std: f64,
    pub keystroke_interval_trend: f64,
    pub mouse_move_count: usize,
    pub mouse_distance_pixels: f64,
    pub mouse_speed_mean: f64,
    pub mouse_speed_std: f64,
    pub mouse_acceleration_mean: f64,
    pub mouse_click_count: usize,
    pub context_switches_30s: usize,
    pub context_switches_5min: usize,
    pub time_in_current_app: i64,
    pub unique_apps_5min: usize,
    pub idle_time_30s: f64,
    pub idle_event_count_5min: usize,
    pub longest_active_stretch_5min: i64,
    pub window_title_length: usize,
    pub window_title_changed_30s: bool,
    pub is_browser: bool,
    pub is_ide: bool,
    pub is_communication: bool,
    pub is_entertainment: bool,
    pub is_productivity: bool,
    pub focus_momentum: f64,
    pub productivity_category: String,
    pub is_pseudo_productive: bool,
}

impl FeatureVector {
    pub fn empty(timestamp: f64) -> Self {
        let dt = Utc::now();
        Self {
            timestamp,
            seconds_since_session_start: 0,
            hour_of_day: dt.hour(),
            day_of_week: dt.weekday().num_days_from_monday(),
            minutes_since_last_break: 0,
            keystroke_count: 0,
            keystroke_rate: 0.0,
            keystroke_interval_mean: 0.0,
            keystroke_interval_std: 0.0,
            keystroke_interval_trend: 0.0,
            mouse_move_count: 0,
            mouse_distance_pixels: 0.0,
            mouse_speed_mean: 0.0,
            mouse_speed_std: 0.0,
            mouse_acceleration_mean: 0.0,
            mouse_click_count: 0,
            context_switches_30s: 0,
            context_switches_5min: 0,
            time_in_current_app: 0,
            unique_apps_5min: 0,
            idle_time_30s: 0.0,
            idle_event_count_5min: 0,
            longest_active_stretch_5min: 0,
            window_title_length: 0,
            window_title_changed_30s: false,
            is_browser: false,
            is_ide: false,
            is_communication: false,
            is_entertainment: false,
            is_productivity: false,
            focus_momentum: 0.0,
            productivity_category: "Unknown".to_string(),
            is_pseudo_productive: false,
        }
    }
}

fn mean(values: &[f64]) -> f64 {
    if values.is_empty() {
        return 0.0;
    }
    values.iter().sum::<f64>() / values.len() as f64
}

fn std_dev(values: &[f64]) -> f64 {
    if values.len() < 2 {
        return 0.0;
    }
    let avg = mean(values);
    let variance = values.iter().map(|v| (v - avg).powi(2)).sum::<f64>() / (values.len() - 1) as f64;
    variance.sqrt()
}

fn linear_slope(values: &[f64]) -> f64 {
    if values.len() < 2 {
        return 0.0;
    }
    let xs: Vec<f64> = (0..values.len()).map(|i| i as f64).collect();
    let mean_x = mean(&xs);
    let mean_y = mean(values);
    let num: f64 = xs
        .iter()
        .zip(values.iter())
        .map(|(x, y)| (x - mean_x) * (y - mean_y))
        .sum();
    let den: f64 = xs.iter().map(|x| (x - mean_x).powi(2)).sum();
    if den == 0.0 {
        0.0
    } else {
        num / den
    }
}

fn classify_app(app_name: &str) -> (bool, bool, bool, bool, bool) {
    let name = app_name.to_lowercase();
    let browsers = ["chrome", "msedge", "firefox", "brave", "opera", "safari"];
    let ides = [
        "code",
        "cursor",
        "devenv",
        "idea",
        "pycharm",
        "clion",
        "rider",
        "xcode",
    ];
    let comm = ["slack", "discord", "teams", "outlook", "zoom", "messages"];
    let ent = ["spotify", "steam", "vlc", "netflix", "youtube"];
    let prod = ["word", "excel", "powerpnt", "notion", "obsidian", "pages"];

    let is_browser = browsers.iter().any(|b| name.contains(b));
    let is_ide = ides.iter().any(|b| name.contains(b));
    let is_comm = comm.iter().any(|b| name.contains(b));
    let is_ent = ent.iter().any(|b| name.contains(b));
    let is_prod = prod.iter().any(|b| name.contains(b));
    (is_browser, is_ide, is_comm, is_ent, is_prod)
}

pub struct FeatureExtractor {
    window_seconds: f64,
    long_window_seconds: f64,
    break_threshold_seconds: f64,
    events_30s: VecDeque<CaptureEvent>,
    events_5min: VecDeque<CaptureEvent>,
    session_start_ts: Option<f64>,
    last_break_ts: Option<f64>,
    current_app_name: String,
    current_app_start_ts: Option<f64>,
    focus_momentum: f64,
}

impl FeatureExtractor {
    pub fn new() -> Self {
        Self {
            window_seconds: 30.0,
            long_window_seconds: 300.0,
            break_threshold_seconds: 300.0,
            events_30s: VecDeque::new(),
            events_5min: VecDeque::new(),
            session_start_ts: None,
            last_break_ts: None,
            current_app_name: String::new(),
            current_app_start_ts: None,
            focus_momentum: 0.0,
        }
    }

    pub fn update_focus_score(&mut self, score: f64, alpha: f64) {
        self.focus_momentum = alpha * score + (1.0 - alpha) * self.focus_momentum;
    }

    pub fn update(&mut self, event: &CaptureEvent) -> FeatureVector {
        let now = event.timestamp_secs;
        if self.session_start_ts.is_none() {
            self.session_start_ts = Some(now);
            self.last_break_ts = Some(now);
            self.current_app_name = event.app_name.clone();
            self.current_app_start_ts = Some(now);
        }

        self.events_30s.push_back(event.clone());
        self.events_5min.push_back(event.clone());
        self.trim(now);
        self.update_break_state(event, now);
        self.update_current_app(event, now);
        self.extract_features(now)
    }

    pub fn extract_features(&self, now: f64) -> FeatureVector {
        if self.events_5min.is_empty() {
            return FeatureVector::empty(now);
        }

        let oldest_30s = self.events_30s.front().map(|e| e.timestamp_secs).unwrap_or(now);
        let span_30s = (self.window_seconds.min(now - oldest_30s)).max(1e-6);

        let keystrokes: Vec<&CaptureEvent> = self
            .events_30s
            .iter()
            .filter(|e| e.event_type == EventType::KeyPress)
            .collect();
        let key_times: Vec<f64> = keystrokes.iter().map(|e| e.timestamp_secs).collect();
        let intervals: Vec<f64> = key_times
            .windows(2)
            .map(|w| w[1] - w[0])
            .collect();

        let mouse_moves: Vec<&CaptureEvent> = self
            .events_30s
            .iter()
            .filter(|e| e.event_type == EventType::MouseMove)
            .collect();
        let mouse_clicks: Vec<&CaptureEvent> = self
            .events_30s
            .iter()
            .filter(|e| e.event_type == EventType::MouseClick)
            .collect();

        let mut speeds = Vec::new();
        let mut distances = Vec::new();
        let mut accelerations = Vec::new();
        for (i, event) in mouse_moves.iter().enumerate() {
            let speed = event.mouse_speed as f64;
            speeds.push(speed);
            if i > 0 {
                let prev = mouse_moves[i - 1];
                let dt = (event.timestamp_secs - prev.timestamp_secs).max(1e-6);
                distances.push(speed * dt);
                let prev_speed = speeds[i - 1];
                accelerations.push((speed - prev_speed).abs() / dt);
            }
        }

        let context_switches_30s = self
            .events_30s
            .iter()
            .filter(|e| e.event_type == EventType::WindowFocusChange)
            .count();
        let context_switches_5min = self
            .events_5min
            .iter()
            .filter(|e| e.event_type == EventType::WindowFocusChange)
            .count();
        let unique_apps_5min = self
            .events_5min
            .iter()
            .map(|e| e.app_name.as_str())
            .collect::<std::collections::HashSet<_>>()
            .len();

        let time_in_current_app = self
            .current_app_start_ts
            .map(|start| (now - start) as i64)
            .unwrap_or(0);

        let idle_events_30s: Vec<&CaptureEvent> = self
            .events_30s
            .iter()
            .filter(|e| matches!(e.event_type, EventType::IdleStart | EventType::IdleEnd))
            .collect();
        let idle_time_30s: f64 = idle_events_30s
            .iter()
            .map(|e| e.idle_duration_ms as f64 / 1000.0)
            .sum();

        let idle_events_5min: Vec<&CaptureEvent> = self
            .events_5min
            .iter()
            .filter(|e| matches!(e.event_type, EventType::IdleStart | EventType::IdleEnd))
            .collect();
        let idle_timestamps: Vec<f64> = idle_events_5min.iter().map(|e| e.timestamp_secs).collect();
        let longest_active_stretch_5min = if idle_timestamps.is_empty() {
            self.long_window_seconds as i64
        } else {
            let window_start = now - self.long_window_seconds;
            let mut boundaries = vec![window_start];
            boundaries.extend(idle_timestamps);
            boundaries.push(now);
            boundaries
                .windows(2)
                .map(|w| (w[1] - w[0]) as i64)
                .max()
                .unwrap_or(0)
        };

        let window_title_changed_30s = self
            .events_30s
            .iter()
            .any(|e| e.event_type == EventType::WindowTitleChange);

        let (is_browser, is_ide, is_comm, is_ent, is_prod) =
            classify_app(&self.current_app_name);
        let productivity_category = if is_ide {
            "Building"
        } else if is_prod {
            "Writing"
        } else if is_browser {
            "Browsing"
        } else if is_comm {
            "Communicating"
        } else if is_ent {
            "Entertainment"
        } else {
            "Unknown"
        }
        .to_string();

        let minutes_since_last_break = self
            .last_break_ts
            .map(|ts| ((now - ts) / 60.0).max(0.0) as i64)
            .unwrap_or(0);

        let dt = chrono::DateTime::<Utc>::from_timestamp(now as i64, 0).unwrap_or_else(Utc::now);

        FeatureVector {
            timestamp: now,
            seconds_since_session_start: (now - self.session_start_ts.unwrap_or(now)) as i64,
            hour_of_day: dt.hour(),
            day_of_week: dt.weekday().num_days_from_monday(),
            minutes_since_last_break,
            keystroke_count: keystrokes.len(),
            keystroke_rate: keystrokes.len() as f64 / span_30s,
            keystroke_interval_mean: mean(&intervals),
            keystroke_interval_std: std_dev(&intervals),
            keystroke_interval_trend: linear_slope(&intervals),
            mouse_move_count: mouse_moves.len(),
            mouse_distance_pixels: distances.iter().sum(),
            mouse_speed_mean: mean(&speeds),
            mouse_speed_std: std_dev(&speeds),
            mouse_acceleration_mean: mean(&accelerations),
            mouse_click_count: mouse_clicks.len(),
            context_switches_30s,
            context_switches_5min,
            time_in_current_app,
            unique_apps_5min,
            idle_time_30s,
            idle_event_count_5min: idle_events_5min.len(),
            longest_active_stretch_5min,
            window_title_length: self.current_app_name.len(),
            window_title_changed_30s,
            is_browser,
            is_ide,
            is_communication: is_comm,
            is_entertainment: is_ent,
            is_productivity: is_prod,
            focus_momentum: self.focus_momentum,
            productivity_category,
            is_pseudo_productive: false,
        }
    }

    fn trim(&mut self, now: f64) {
        while self
            .events_30s
            .front()
            .is_some_and(|e| now - e.timestamp_secs > self.window_seconds)
        {
            self.events_30s.pop_front();
        }
        while self
            .events_5min
            .front()
            .is_some_and(|e| now - e.timestamp_secs > self.long_window_seconds)
        {
            self.events_5min.pop_front();
        }
    }

    fn update_break_state(&mut self, event: &CaptureEvent, now: f64) {
        if matches!(event.event_type, EventType::IdleStart | EventType::IdleEnd) {
            let duration = event.idle_duration_ms as f64 / 1000.0;
            if duration >= self.break_threshold_seconds {
                self.last_break_ts = Some(now);
            }
        }
    }

    fn update_current_app(&mut self, event: &CaptureEvent, now: f64) {
        if event.event_type == EventType::WindowFocusChange {
            self.current_app_name = event.app_name.clone();
            self.current_app_start_ts = Some(now);
        }
    }
}

impl Default for FeatureExtractor {
    fn default() -> Self {
        Self::new()
    }
}
