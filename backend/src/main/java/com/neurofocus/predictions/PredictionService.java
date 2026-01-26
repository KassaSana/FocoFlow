package com.neurofocus.predictions;

import java.time.Instant;
import java.util.concurrent.atomic.AtomicReference;

import org.springframework.context.ApplicationEventPublisher;
import org.springframework.stereotype.Service;

@Service
public class PredictionService {
    private final AtomicReference<PredictionRecord> latest = new AtomicReference<>();
    private final ApplicationEventPublisher eventPublisher;

    public PredictionService(ApplicationEventPublisher eventPublisher) {
        this.eventPublisher = eventPublisher;
    }

    public PredictionRecord save(PredictionRequest request) {
        PredictionRecord record = new PredictionRecord(
            request.sessionId(),
            request.focusScore(),
            request.distractionRisk(),
            Instant.now()
        );
        latest.set(record);
        eventPublisher.publishEvent(new PredictionEvent(record));
        return record;
    }

    public PredictionRecord getLatest() {
        return latest.get();
    }
}
