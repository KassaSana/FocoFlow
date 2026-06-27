use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum EventType {
    KeyPress = 1,
    KeyRelease = 2,
    MouseMove = 3,
    MouseClick = 4,
    WindowFocusChange = 5,
    WindowTitleChange = 6,
    IdleStart = 7,
    IdleEnd = 8,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CaptureEvent {
    pub event_type: EventType,
    pub timestamp_secs: f64,
    pub app_name: String,
    pub window_title: String,
    pub mouse_x: i32,
    pub mouse_y: i32,
    pub mouse_speed: u32,
    pub idle_duration_ms: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PredictionRecord {
    pub session_id: String,
    pub focus_score: f64,
    pub distraction_risk: f64,
    pub focus_state: String,
    pub timestamp: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionRecord {
    pub session_id: String,
    pub goal: String,
    pub status: String,
    pub focus_mode: String,
    pub started_at: Option<String>,
    pub ended_at: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ContextSnapshotDto {
    pub app_name: String,
    pub window_title: String,
    pub file_hint: String,
    pub project_hint: String,
    pub summary: String,
    pub timestamp: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SnapbackPayload {
    pub summary: String,
    pub app_name: String,
    pub window_title: String,
    pub file_hint: String,
    pub distraction_duration_secs: u32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PermissionStatus {
    pub capture_available: bool,
    pub active_window_available: bool,
    pub message: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionRecap {
    pub session_id: String,
    pub goal: String,
    pub duration_secs: u64,
    pub avg_focus_score: f64,
    pub avg_distraction_risk: f64,
    pub snapback_count: u32,
    pub thrash_spikes: u32,
    pub deep_focus_pct: f64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum FocusLabel {
    Distracted = -1,
    PseudoProductive = 0,
    Productive = 1,
    DeepFocus = 2,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct LabelRequest {
    pub session_id: String,
    pub label: FocusLabel,
    pub notes: Option<String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum FocusMode {
    Deep,
    Normal,
    Recovery,
}

impl FocusMode {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Deep => "deep",
            Self::Normal => "normal",
            Self::Recovery => "recovery",
        }
    }

    pub fn from_str(s: &str) -> Self {
        match s.to_lowercase().as_str() {
            "deep" => Self::Deep,
            "recovery" => Self::Recovery,
            _ => Self::Normal,
        }
    }

    pub fn risk_threshold(self) -> f64 {
        match self {
            Self::Deep => 0.55,
            Self::Normal => 0.7,
            Self::Recovery => 0.85,
        }
    }

    pub fn hyperfocus_minutes(self) -> u32 {
        match self {
            Self::Deep => 90,
            Self::Normal => 120,
            Self::Recovery => 45,
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HealthStatus {
    pub status: String,
    pub capture_running: bool,
    pub permissions: PermissionStatus,
}
