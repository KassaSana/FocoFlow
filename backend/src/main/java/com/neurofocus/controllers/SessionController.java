package com.neurofocus.controllers;

import com.neurofocus.sessions.SessionRecord;
import com.neurofocus.sessions.SessionService;
import com.neurofocus.sessions.StartSessionRequest;

import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api/sessions")
public class SessionController {
    private final SessionService sessionService;

    public SessionController(SessionService sessionService) {
        this.sessionService = sessionService;
    }

    @PostMapping("/start")
    public ResponseEntity<SessionRecord> start(@RequestBody StartSessionRequest request) {
        if (request == null || request.goal() == null || request.goal().trim().isEmpty()) {
            return ResponseEntity.badRequest().build();
        }
        SessionRecord record = sessionService.startSession(request.goal().trim());
        return ResponseEntity.ok(record);
    }

    @PostMapping("/{sessionId}/stop")
    public ResponseEntity<SessionRecord> stop(@PathVariable String sessionId) {
        SessionRecord record = sessionService.stopSession(sessionId);
        if (record == null) {
            return ResponseEntity.notFound().build();
        }
        return ResponseEntity.ok(record);
    }

    @GetMapping("/{sessionId}")
    public ResponseEntity<SessionRecord> get(@PathVariable String sessionId) {
        SessionRecord record = sessionService.getSession(sessionId);
        if (record == null) {
            return ResponseEntity.notFound().build();
        }
        return ResponseEntity.ok(record);
    }
}
