package com.neurofocus.sessions;

import java.time.Instant;

public record SessionRecord(
    String sessionId,
    String goal,
    SessionStatus status,
    Instant startedAt,
    Instant endedAt
) {
}
