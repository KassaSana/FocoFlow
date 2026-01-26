package com.neurofocus.controllers;

import java.time.Instant;
import java.util.HashMap;
import java.util.Map;

import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
public class HealthController {
    @GetMapping({"/health", "/api/health"})
    public Map<String, Object> health() {
        Map<String, Object> payload = new HashMap<>();
        payload.put("status", "UP");
        payload.put("service", "neurofocus-backend");
        payload.put("timestamp", Instant.now().toString());
        return payload;
    }
}
