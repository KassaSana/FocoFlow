package com.neurofocus.websocket;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.neurofocus.predictions.PredictionEvent;
import com.neurofocus.predictions.PredictionRecord;
import com.neurofocus.predictions.PredictionService;

import java.io.IOException;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

@Component
public class PredictionStreamHandler extends TextWebSocketHandler {
    private final PredictionService predictionService;
    private final ObjectMapper objectMapper;
    private final Set<WebSocketSession> sessions = ConcurrentHashMap.newKeySet();

    public PredictionStreamHandler(PredictionService predictionService, ObjectMapper objectMapper) {
        this.predictionService = predictionService;
        this.objectMapper = objectMapper;
    }

    @Override
    public void afterConnectionEstablished(WebSocketSession session) throws Exception {
        sessions.add(session);
        PredictionRecord latest = predictionService.getLatest();
        if (latest != null) {
            sendMessage(session, latest);
        }
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, org.springframework.web.socket.CloseStatus status) {
        sessions.remove(session);
    }

    @Override
    public void handleTransportError(WebSocketSession session, Throwable exception) {
        sessions.remove(session);
    }

    @EventListener
    public void onPredictionEvent(PredictionEvent event) {
        broadcast(event.record());
    }

    // Compatibility method for tests or listeners expecting ApplicationListener-style callback
    public void onApplicationEvent(PredictionEvent event) {
        onPredictionEvent(event);
    }

    private void broadcast(PredictionRecord record) {
        for (WebSocketSession session : sessions) {
            if (session.isOpen()) {
                sendMessage(session, record);
            }
        }
    }

    private void sendMessage(WebSocketSession session, PredictionRecord record) {
        try {
            String json = objectMapper.writeValueAsString(record);
            session.sendMessage(new TextMessage(json));
        } catch (IOException ignored) {
            sessions.remove(session);
        }
    }
}
