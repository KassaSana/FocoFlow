package com.neurofocus.controllers;

import com.neurofocus.predictions.PredictionRecord;
import com.neurofocus.predictions.PredictionRequest;
import com.neurofocus.predictions.PredictionService;

import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api/predictions")
public class PredictionController {
    private final PredictionService predictionService;

    public PredictionController(PredictionService predictionService) {
        this.predictionService = predictionService;
    }

    @GetMapping("/latest")
    public ResponseEntity<PredictionRecord> latest() {
        PredictionRecord record = predictionService.getLatest();
        if (record == null) {
            return ResponseEntity.notFound().build();
        }
        return ResponseEntity.ok(record);
    }

    @PostMapping
    public ResponseEntity<PredictionRecord> create(@RequestBody PredictionRequest request) {
        if (request == null) {
            return ResponseEntity.badRequest().build();
        }
        if (request.distractionRisk() < 0.0 || request.distractionRisk() > 1.0) {
            return ResponseEntity.badRequest().build();
        }
        if (request.focusScore() < 0.0 || request.focusScore() > 100.0) {
            return ResponseEntity.badRequest().build();
        }
        PredictionRecord record = predictionService.save(request);
        return ResponseEntity.ok(record);
    }
}
