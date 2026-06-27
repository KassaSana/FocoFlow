use crate::engine::features::FeatureVector;
use crate::types::FocusMode;

#[derive(Debug, Clone)]
pub struct PredictionScores {
    pub focus_score: f64,
    pub distraction_risk: f64,
    pub focus_state: String,
}

const FOCUS_LEVELS: [f64; 4] = [25.0, 50.0, 75.0, 100.0];

fn clamp(value: f64, min: f64, max: f64) -> f64 {
    value.max(min).min(max)
}

fn scores_from_probas(probas: [f64; 4]) -> PredictionScores {
    let total: f64 = probas.iter().sum();
    let probas = if total <= 0.0 {
        [0.25, 0.25, 0.25, 0.25]
    } else {
        probas.map(|p| p / total)
    };
    let distraction_risk = clamp(probas[0], 0.0, 1.0);
    let focus_score: f64 = probas
        .iter()
        .enumerate()
        .map(|(i, p)| FOCUS_LEVELS[i] * p)
        .sum();

    let focus_state = if distraction_risk >= 0.7 {
        "DISTRACTED"
    } else if focus_score >= 85.0 {
        "DEEP_FOCUS"
    } else if focus_score >= 60.0 {
        "PRODUCTIVE"
    } else if focus_score >= 40.0 {
        "PSEUDO_PRODUCTIVE"
    } else {
        "DISTRACTED"
    }
    .to_string();

    PredictionScores {
        focus_score,
        distraction_risk,
        focus_state,
    }
}

fn heuristic_probas(features: &FeatureVector) -> [f64; 4] {
    let context_switches = features.context_switches_30s as f64;
    let idle_time = features.idle_time_30s;
    let keystroke_rate = features.keystroke_rate;
    let time_in_app = features.time_in_current_app as f64;
    let is_entertainment = if features.is_entertainment { 1.0 } else { 0.0 };
    let is_communication = if features.is_communication { 1.0 } else { 0.0 };

    let mut risk = 0.0;
    risk += (context_switches / 4.0).min(1.0) * 0.3;
    risk += (idle_time / 8.0).min(1.0) * 0.3;
    risk += (1.0 - (keystroke_rate / 4.0).min(1.0)) * 0.2;
    risk += (1.0 - (time_in_app / 120.0).min(1.0)) * 0.1;
    risk += is_entertainment * 0.05;
    risk += is_communication * 0.05;
    risk = clamp(risk, 0.0, 1.0);

    let remaining = 1.0 - risk;
    [risk, remaining * 0.2, remaining * 0.5, remaining * 0.3]
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

    pub fn predict(&self, features: &FeatureVector) -> PredictionScores {
        let mut scores = scores_from_probas(heuristic_probas(features));

        #[cfg(feature = "onnx")]
        if let Some(onnx_scores) = self.try_onnx_predict(features) {
            scores = onnx_scores;
        }

        let threshold = self.focus_mode.risk_threshold();
        if scores.distraction_risk >= threshold {
            scores.focus_state = "DISTRACTED".to_string();
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

    #[test]
    fn heuristic_low_risk_when_stable() {
        let features = FeatureVector {
            context_switches_30s: 0,
            idle_time_30s: 0.0,
            keystroke_rate: 3.0,
            time_in_current_app: 200,
            is_entertainment: false,
            is_communication: false,
            ..FeatureVector::empty(0.0)
        };
        let scores = Classifier::new(FocusMode::Normal).predict(&features);
        assert!(scores.distraction_risk < 0.5);
    }
}
