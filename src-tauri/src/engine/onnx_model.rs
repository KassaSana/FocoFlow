#[cfg(feature = "onnx")]
use crate::engine::features::FeatureVector;
#[cfg(feature = "onnx")]
use crate::engine::classifier::PredictionScores;

#[cfg(feature = "onnx")]
pub fn predict(_features: &FeatureVector) -> Option<PredictionScores> {
    // Placeholder: load `model.onnx` from app data dir and run `ort` session.
    // Build with: cargo build --features onnx
    None
}
