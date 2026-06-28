use crate::engine::app_context::classify;
use crate::engine::features::FeatureVector;
use crate::engine::goal_alignment::{alignment_bias, alignment_score};
use crate::types::{AppRuleRecord, FocusMode};

#[derive(Debug, Clone)]
pub struct PredictionScores {
    pub focus_score: f64,
    pub distraction_risk: f64,
    pub focus_state: String,
    pub thrash_score: f64,
    pub drift_score: f64,
    pub goal_alignment: f64,
}

const FOCUS_LEVELS: [f64; 4] = [25.0, 50.0, 75.0, 100.0];
const STATE_LABELS: [&str; 4] = [
    "DISTRACTED",
    "PSEUDO_PRODUCTIVE",
    "PRODUCTIVE",
    "DEEP_FOCUS",
];

fn clamp(value: f64, min: f64, max: f64) -> f64 {
    value.max(min).min(max)
}

/// Rapid context jumping across apps/windows (thrash).
fn thrash_score(features: &FeatureVector) -> f64 {
    let switches_30s = (features.context_switches_30s as f64 / 4.0).min(1.0);
    let switches_5min = (features.context_switches_5min as f64 / 10.0).min(1.0);
    let unique_apps = ((features.unique_apps_5min.saturating_sub(1)) as f64 / 5.0)
        .min(1.0)
        .max(0.0);
    clamp(switches_30s * 0.45 + switches_5min * 0.25 + unique_apps * 0.30, 0.0, 1.0)
}

/// Busy-work drift: churning tabs/titles or erratic typing while still in "work" apps.
fn drift_score(features: &FeatureVector) -> f64 {
    let title_churn = if features.window_title_changed_30s { 1.0 } else { 0.0 };
    let keystroke_chaos = if features.keystroke_count >= 3 {
        (features.keystroke_interval_std / 1.2).min(1.0)
    } else {
        0.0
    };
    let unsettled = (features.context_switches_30s as f64 / 3.0).min(1.0);
    let in_work_context = if features.is_ide || features.is_productivity {
        1.0
    } else if features.is_browser || features.is_communication {
        0.85
    } else {
        0.4
    };
    clamp(
        (title_churn * 0.45 + keystroke_chaos * 0.30 + unsettled * 0.25) * in_work_context,
        0.0,
        1.0,
    )
}

/// Stable deep-work signal: low thrash/drift, sustained time, steady typing.
fn deep_work_score(features: &FeatureVector, thrash: f64, drift: f64) -> f64 {
    let settled = (features.time_in_current_app as f64 / 180.0).min(1.0);
    let steady_typing = if features.keystroke_count >= 4 {
        (1.0 - (features.keystroke_interval_std / 1.0).min(1.0)).max(0.0)
    } else {
        0.3
    };
    let low_switch = (1.0 - (features.context_switches_30s as f64 / 2.0).min(1.0)).max(0.0);
    let stability = (1.0 - thrash) * (1.0 - drift);
    clamp(
        settled * 0.35 + steady_typing * 0.25 + low_switch * 0.20 + stability * 0.20,
        0.0,
        1.0,
    )
}

fn heuristic_probas(
    features: &FeatureVector,
    session_goal: Option<&str>,
    rules: &[AppRuleRecord],
) -> ([f64; 4], f64, f64) {
    let ctx = classify(&features.app_name, &features.window_title, rules);
    let bias = alignment_bias(session_goal, &ctx, &features.window_title);

    let thrash = thrash_score(features);
    let mut drift = drift_score(features);
    drift = clamp(drift - bias * 0.25, 0.0, 1.0);
    let deep = deep_work_score(features, thrash, drift);

    let idle_time = features.idle_time_30s;
    let keystroke_rate = features.keystroke_rate;
    let time_in_app = features.time_in_current_app as f64;
    let is_entertainment = if ctx.is_entertainment || ctx.personal_block {
        1.0
    } else {
        0.0
    };
    let is_communication = if ctx.is_communication && !ctx.personal_allow {
        1.0
    } else {
        0.0
    };

    let mut distracted = 0.0;
    distracted += thrash * 0.30;
    distracted += (idle_time / 8.0).min(1.0) * 0.15;
    distracted += (1.0 - (keystroke_rate / 4.0).min(1.0)) * 0.10;
    distracted += (1.0 - (time_in_app / 120.0).min(1.0)) * 0.10;
    distracted += is_entertainment * 0.20;
    distracted += is_communication * 0.05;
    distracted = clamp(distracted - bias * 0.35, 0.0, 1.0);

    let pseudo = clamp(drift * (1.0 - distracted * 0.6), 0.0, 1.0);
    let remaining = (1.0 - distracted - pseudo).max(0.0);
    let productive = remaining * (1.0 - deep * 0.65);
    let deep_prob = remaining * deep * 0.65;

    (
        [distracted, pseudo, productive, deep_prob.max(0.0)],
        thrash,
        drift,
    )
}

fn scores_from_probas(
    probas: [f64; 4],
    thrash: f64,
    drift: f64,
    goal_alignment: f64,
) -> PredictionScores {
    let total: f64 = probas.iter().sum();
    let probas = if total <= 0.0 {
        [0.25, 0.25, 0.25, 0.25]
    } else {
        probas.map(|p| p / total)
    };

    let distraction_risk = clamp(probas[0] + thrash * 0.15, 0.0, 1.0);
    let focus_score: f64 = probas
        .iter()
        .enumerate()
        .map(|(i, p)| FOCUS_LEVELS[i] * p)
        .sum();

    let focus_state = probas
        .iter()
        .enumerate()
        .max_by(|a, b| a.1.partial_cmp(b.1).unwrap_or(std::cmp::Ordering::Equal))
        .map(|(idx, _)| STATE_LABELS[idx].to_string())
        .unwrap_or_else(|| "UNKNOWN".to_string());

    PredictionScores {
        focus_score,
        distraction_risk,
        focus_state,
        thrash_score: thrash,
        drift_score: drift,
        goal_alignment,
    }
}

pub struct Classifier {
    focus_mode: FocusMode,
}

impl Classifier {
    pub fn new(focus_mode: FocusMode) -> Self {
        Self { focus_mode }
    }

    pub fn set_focus_mode(&mut self, mode: FocusMode) {
        self.focus_mode = mode;
    }

    pub fn predict(
        &self,
        features: &FeatureVector,
        session_goal: Option<&str>,
        rules: &[AppRuleRecord],
    ) -> PredictionScores {
        let ctx = classify(&features.app_name, &features.window_title, rules);
        let goal_alignment = session_goal
            .filter(|g| !g.trim().is_empty())
            .map(|g| alignment_score(g, &ctx, &features.window_title))
            .unwrap_or(0.5);

        let (probas, thrash, drift) = heuristic_probas(features, session_goal, rules);
        let mut scores = scores_from_probas(probas, thrash, drift, goal_alignment);

        #[cfg(feature = "onnx")]
        if let Some(onnx_scores) = self.try_onnx_predict(features) {
            scores = onnx_scores;
        }

        let threshold = self.focus_mode.risk_threshold();
        if scores.distraction_risk >= threshold || thrash >= 0.75 || ctx.personal_block {
            scores.focus_state = "DISTRACTED".to_string();
        } else if drift >= 0.55 && scores.focus_state != "DEEP_FOCUS" {
            scores.focus_state = "PSEUDO_PRODUCTIVE".to_string();
        }

        scores
    }

    #[cfg(feature = "onnx")]
    fn try_onnx_predict(&self, features: &FeatureVector) -> Option<PredictionScores> {
        crate::engine::onnx_model::predict(features)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::engine::features::FeatureVector;

    fn stable_features() -> FeatureVector {
        FeatureVector {
            app_name: "Cursor".to_string(),
            window_title: "classifier.rs — Snapback".to_string(),
            context_switches_30s: 0,
            context_switches_5min: 1,
            unique_apps_5min: 1,
            idle_time_30s: 0.0,
            keystroke_rate: 3.0,
            keystroke_count: 8,
            keystroke_interval_std: 0.15,
            time_in_current_app: 200,
            window_title_changed_30s: false,
            is_entertainment: false,
            is_communication: false,
            is_ide: true,
            is_productivity: false,
            ..FeatureVector::empty(0.0)
        }
    }

    #[test]
    fn stable_work_scores_low_thash_and_drift() {
        let scores = Classifier::new(FocusMode::Normal).predict(&stable_features(), None, &[]);
        assert!(scores.thrash_score < 0.35, "thrash={}", scores.thrash_score);
        assert!(scores.drift_score < 0.35, "drift={}", scores.drift_score);
        assert!(scores.distraction_risk < 0.5);
    }

    #[test]
    fn thrash_from_many_switches_and_apps() {
        let features = FeatureVector {
            context_switches_30s: 5,
            context_switches_5min: 12,
            unique_apps_5min: 6,
            is_entertainment: true,
            ..stable_features()
        };
        let scores = Classifier::new(FocusMode::Normal).predict(&features, None, &[]);
        assert!(scores.thrash_score >= 0.75, "thrash={}", scores.thrash_score);
        assert_eq!(scores.focus_state, "DISTRACTED");
    }

    #[test]
    fn drift_from_title_churn_in_ide() {
        let features = FeatureVector {
            context_switches_30s: 2,
            window_title_changed_30s: true,
            keystroke_interval_std: 0.9,
            keystroke_count: 10,
            is_ide: true,
            unique_apps_5min: 2,
            ..stable_features()
        };
        let scores = Classifier::new(FocusMode::Normal).predict(&features, None, &[]);
        assert!(scores.drift_score >= 0.55, "drift={}", scores.drift_score);
        assert_eq!(scores.focus_state, "PSEUDO_PRODUCTIVE");
    }

    #[test]
    fn deep_focus_when_stable_and_settled() {
        let scores = Classifier::new(FocusMode::Normal).predict(&stable_features(), None, &[]);
        assert!(
            scores.focus_state == "DEEP_FOCUS" || scores.focus_state == "PRODUCTIVE",
            "state={}",
            scores.focus_state
        );
        assert!(scores.focus_score >= 60.0);
    }

    #[test]
    fn coding_goal_reduces_penalty_in_ide() {
        let features = FeatureVector {
            context_switches_30s: 2,
            window_title_changed_30s: true,
            keystroke_interval_std: 0.7,
            keystroke_count: 8,
            is_ide: true,
            app_name: "Cursor".to_string(),
            window_title: "classifier.rs".to_string(),
            ..stable_features()
        };
        let without_goal = Classifier::new(FocusMode::Normal).predict(&features, None, &[]);
        let with_goal = Classifier::new(FocusMode::Normal).predict(
            &features,
            Some("implement the rust classifier feature"),
            &[],
        );
        assert!(with_goal.goal_alignment > without_goal.goal_alignment);
        assert!(with_goal.drift_score <= without_goal.drift_score);
    }

    #[test]
    fn personal_block_rule_increases_distraction() {
        let features = FeatureVector {
            app_name: "Notion".to_string(),
            window_title: "Weekly plan".to_string(),
            is_productivity: true,
            is_ide: false,
            is_entertainment: false,
            keystroke_rate: 2.0,
            time_in_current_app: 120,
            ..FeatureVector::empty(0.0)
        };
        let rules = vec![crate::types::AppRuleRecord {
            id: 1,
            pattern: "notion".to_string(),
            rule_type: crate::types::AppRuleKind::Block,
            note: None,
            created_at: String::new(),
            updated_at: String::new(),
        }];
        let default_scores = Classifier::new(FocusMode::Normal).predict(&features, None, &[]);
        let blocked_scores = Classifier::new(FocusMode::Normal).predict(&features, None, &rules);
        assert!(blocked_scores.distraction_risk >= default_scores.distraction_risk);
        assert_eq!(blocked_scores.focus_state, "DISTRACTED");
    }
}
