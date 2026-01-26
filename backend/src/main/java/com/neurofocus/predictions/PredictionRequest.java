package com.neurofocus.predictions;

public record PredictionRequest(
    String sessionId,
    double focusScore,
    double distractionRisk
) {
}
