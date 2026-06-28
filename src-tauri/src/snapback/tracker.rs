use std::time::{Instant, SystemTime, UNIX_EPOCH};

use crate::engine::app_context::{classify, snapback_on_task};
use crate::snapback::title_parser::{parse_window_title, ParsedTitle};
use crate::types::{AppRuleRecord, ContextSnapshotDto};

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
    /// Latest label from the classifier — shared brain with the dashboard.
    latest_focus_state: Option<String>,
    latest_session_goal: Option<String>,
    latest_app_rules: Vec<AppRuleRecord>,
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
            latest_focus_state: None,
            latest_session_goal: None,
            latest_app_rules: Vec::new(),
        }
    }

    pub fn set_app_rules(&mut self, rules: &[AppRuleRecord]) {
        self.latest_app_rules.clear();
        self.latest_app_rules.extend_from_slice(rules);
    }

    pub fn state(&self) -> DistractionState {
        self.state
    }

    /// Called ~once per second from the engine loop after `Classifier::predict`.
    pub fn on_prediction_feedback(&mut self, focus_state: &str, session_goal: Option<&str>) {
        self.latest_focus_state = Some(focus_state.to_string());
        self.latest_session_goal = session_goal.map(|g| g.to_string());
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
        let was_on_task = self.is_on_task(&self.current.app_name, &self.current.window_title);
        let now_on_task = self.is_on_task(app_name, window_title);

        if self.state == DistractionState::Focused && self.current.is_meaningful() {
            self.last_focus_snapshot = Some(self.current.clone());
        }

        match self.state {
            DistractionState::Focused if was_on_task && !now_on_task => {
                self.state = DistractionState::Distracted;
                self.distraction_started = Some(Instant::now());
                self.distraction_app = app_name.to_string();
            }
            DistractionState::Distracted if now_on_task => {
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
            DistractionState::Recovering if was_on_task && !now_on_task => {
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
            if self.current.is_meaningful() && self.is_on_task(&self.current.app_name, &self.current.window_title) {
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

    fn is_on_task(&self, app_name: &str, window_title: &str) -> bool {
        let ctx = classify(app_name, window_title, &self.latest_app_rules);
        snapback_on_task(
            &ctx,
            window_title,
            self.latest_focus_state.as_deref(),
            self.latest_session_goal.as_deref(),
        )
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

#[allow(dead_code)]
fn now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn leaving_ide_for_youtube_enters_distracted() {
        let mut tracker = ContextTracker::new();
        tracker.on_window_change("Cursor", "main.rs — Snapback");
        tracker.on_prediction_feedback("PRODUCTIVE", None);

        tracker.on_window_change("Google Chrome", "Funny cats - YouTube");

        assert_eq!(tracker.state(), DistractionState::Distracted);
    }

    #[test]
    fn short_distraction_does_not_snapback() {
        let mut tracker = ContextTracker::new();
        tracker.on_window_change("Cursor", "main.rs");
        tracker.on_prediction_feedback("DEEP_FOCUS", None);
        tracker.on_window_change("Google Chrome", "YouTube");
        tracker.on_window_change("Cursor", "main.rs");

        assert_eq!(tracker.state(), DistractionState::Focused);
        assert!(tracker.take_pending_snapback().is_none());
    }

    #[test]
    fn returning_to_slack_while_classifier_distracted_stays_distracted() {
        let mut tracker = ContextTracker::new();
        tracker.min_distraction_ms = 0;
        tracker.on_window_change("Cursor", "lib.rs");
        tracker.on_prediction_feedback("PRODUCTIVE", Some("implement the api"));
        tracker.on_window_change("Google Chrome", "YouTube");
        tracker.on_prediction_feedback("DISTRACTED", Some("implement the api"));
        tracker.on_window_change("Slack", "#random");

        assert_eq!(tracker.state(), DistractionState::Distracted);
        assert!(tracker.take_pending_snapback().is_none());
    }
}
