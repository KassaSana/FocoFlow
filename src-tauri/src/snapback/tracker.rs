use std::time::{Instant, SystemTime, UNIX_EPOCH};

use crate::snapback::title_parser::{parse_window_title, ParsedTitle};
use crate::types::ContextSnapshotDto;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DistractionState {
    Focused,
    Distracted,
    Recovering,
}

#[derive(Debug, Clone)]
pub struct SnapbackEvent {
    pub summary: String,
    pub app_name: String,
    pub window_title: String,
    pub file_hint: String,
    pub distraction_duration_secs: u32,
}

#[derive(Debug, Clone)]
struct ContextSnapshot {
    app_name: String,
    window_title: String,
    parsed: ParsedTitle,
    timestamp: String,
}

pub struct ContextTracker {
    state: DistractionState,
    current: ContextSnapshot,
    last_focus_snapshot: Option<ContextSnapshot>,
    distraction_started: Option<Instant>,
    distraction_app: String,
    focus_started: Instant,
    last_snapshot_at: Instant,
    snapshot_interval_ms: u64,
    min_distraction_ms: u64,
    pending_snapback: Option<SnapbackEvent>,
}

impl ContextTracker {
    pub fn new() -> Self {
        Self {
            state: DistractionState::Focused,
            current: empty_snapshot(),
            last_focus_snapshot: None,
            distraction_started: None,
            distraction_app: String::new(),
            focus_started: Instant::now(),
            last_snapshot_at: Instant::now(),
            snapshot_interval_ms: 30_000,
            min_distraction_ms: 30_000,
            pending_snapback: None,
        }
    }

    pub fn state(&self) -> DistractionState {
        self.state
    }

    pub fn take_pending_snapback(&mut self) -> Option<SnapbackEvent> {
        self.pending_snapback.take()
    }

    pub fn dismiss_recovery(&mut self) {
        if self.state == DistractionState::Recovering {
            self.state = DistractionState::Focused;
            self.focus_started = Instant::now();
        }
    }

    pub fn on_window_change(&mut self, app_name: &str, window_title: &str) {
        let parsed = parse_window_title(app_name, window_title);
        let was_productive = is_productive(&self.current.app_name, &self.current.window_title);
        let now_productive = is_productive(app_name, window_title);

        if self.state == DistractionState::Focused && self.current.is_meaningful() {
            self.last_focus_snapshot = Some(self.current.clone());
        }

        match self.state {
            DistractionState::Focused if !now_productive => {
                self.state = DistractionState::Distracted;
                self.distraction_started = Some(Instant::now());
                self.distraction_app = app_name.to_string();
            }
            DistractionState::Distracted if now_productive => {
                let duration = self
                    .distraction_started
                    .map(|s| s.elapsed().as_millis() as u64)
                    .unwrap_or(0);
                if duration >= self.min_distraction_ms {
                    self.state = DistractionState::Recovering;
                    self.pending_snapback = self.build_snapback(duration);
                } else {
                    self.state = DistractionState::Focused;
                    self.focus_started = Instant::now();
                }
            }
            DistractionState::Recovering if was_productive && !now_productive => {
                // switched away again while overlay visible
            }
            _ => {}
        }

        self.current = ContextSnapshot {
            app_name: app_name.to_string(),
            window_title: window_title.to_string(),
            parsed,
            timestamp: chrono::Utc::now().to_rfc3339(),
        };
    }

    pub fn on_activity(&mut self) {
        if self.state == DistractionState::Focused
            && self.last_snapshot_at.elapsed().as_millis() as u64 >= self.snapshot_interval_ms
        {
            if self.current.is_meaningful() {
                self.last_focus_snapshot = Some(self.current.clone());
            }
            self.last_snapshot_at = Instant::now();
        }
    }

    pub fn focus_duration_secs(&self) -> u64 {
        if self.state == DistractionState::Focused {
            self.focus_started.elapsed().as_secs()
        } else {
            0
        }
    }

    pub fn current_snapshot_dto(&self) -> ContextSnapshotDto {
        ContextSnapshotDto {
            app_name: self.current.app_name.clone(),
            window_title: self.current.window_title.clone(),
            file_hint: self.current.parsed.file_hint.clone(),
            project_hint: self.current.parsed.project_hint.clone(),
            summary: self.current.parsed.summary.clone(),
            timestamp: self.current.timestamp.clone(),
        }
    }

    fn build_snapback(&self, distraction_ms: u64) -> Option<SnapbackEvent> {
        let snapshot = self.last_focus_snapshot.as_ref()?;
        Some(SnapbackEvent {
            summary: snapshot.parsed.summary.clone(),
            app_name: snapshot.app_name.clone(),
            window_title: snapshot.window_title.clone(),
            file_hint: snapshot.parsed.file_hint.clone(),
            distraction_duration_secs: (distraction_ms / 1000) as u32,
        })
    }
}

impl Default for ContextTracker {
    fn default() -> Self {
        Self::new()
    }
}

impl ContextSnapshot {
    fn is_meaningful(&self) -> bool {
        !self.window_title.is_empty() || !self.app_name.is_empty()
    }
}

fn empty_snapshot() -> ContextSnapshot {
    ContextSnapshot {
        app_name: String::new(),
        window_title: String::new(),
        parsed: ParsedTitle::default(),
        timestamp: chrono::Utc::now().to_rfc3339(),
    }
}

fn is_productive(app_name: &str, window_title: &str) -> bool {
    let name = app_name.to_lowercase();
    let title = window_title.to_lowercase();
    let productive = [
        "code", "cursor", "xcode", "terminal", "iterm", "warp", "notion", "obsidian", "pages",
        "word", "excel", "figma", "slack",
    ];
    let distracting = [
        "youtube", "netflix", "twitter", "reddit", "instagram", "tiktok", "spotify", "steam",
        "discord",
    ];

    if distracting.iter().any(|d| name.contains(d) || title.contains(d)) {
        return false;
    }
    productive.iter().any(|p| name.contains(p))
        || (!title.is_empty() && !name.is_empty())
}

#[allow(dead_code)]
fn now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}
