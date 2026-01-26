package com.neurofocus.predictions;

import java.time.Instant;

public record PredictionRecord(
    String sessionId,
    double focusScore,
    double distractionRisk,
    Instant timestamp
) {
}
