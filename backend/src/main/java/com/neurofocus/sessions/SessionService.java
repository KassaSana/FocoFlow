package com.neurofocus.sessions;

import java.time.Instant;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;

import org.springframework.stereotype.Service;

@Service
public class SessionService {
    private final Map<String, SessionRecord> sessions = new ConcurrentHashMap<>();

    public SessionRecord startSession(String goal) {
        String sessionId = UUID.randomUUID().toString();
        Instant now = Instant.now();
        SessionRecord record = new SessionRecord(sessionId, goal, SessionStatus.ACTIVE, now, null);
        sessions.put(sessionId, record);
        return record;
    }

    public SessionRecord stopSession(String sessionId) {
        return sessions.computeIfPresent(sessionId, (id, record) -> {
            if (record.status() == SessionStatus.COMPLETED) {
                return record;
            }
            return new SessionRecord(
                record.sessionId(),
                record.goal(),
                SessionStatus.COMPLETED,
                record.startedAt(),
                Instant.now()
            );
        });
    }

    public SessionRecord getSession(String sessionId) {
        return sessions.get(sessionId);
    }
}
