use serde::Serialize;

use crate::types::FocusMode;

#[derive(Debug, Clone, Serialize)]
pub struct HyperfocusAlert {
    pub message: String,
    pub focus_duration_secs: u64,
}

pub fn check_hyperfocus(
    focus_mode: FocusMode,
    deep_focus_secs: u64,
    last_alert_secs: u64,
) -> Option<HyperfocusAlert> {
    let threshold_secs = focus_mode.hyperfocus_minutes() as u64 * 60;
    if deep_focus_secs >= threshold_secs && deep_focus_secs.saturating_sub(last_alert_secs) >= 600 {
        Some(HyperfocusAlert {
            message: format!(
                "You've been in deep focus for {} minutes. Consider a short break.",
                deep_focus_secs / 60
            ),
            focus_duration_secs: deep_focus_secs,
        })
    } else {
        None
    }
}
