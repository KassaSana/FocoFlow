package com.neurofocus.websocket;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.neurofocus.predictions.PredictionEvent;
import com.neurofocus.predictions.PredictionRecord;
import com.neurofocus.predictions.PredictionService;

import java.time.Instant;

import org.junit.jupiter.api.Test;
import org.mockito.Mockito;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

class PredictionStreamHandlerTest {
    @Test
    void broadcastsOnPredictionEvent() throws Exception {
        PredictionService service = Mockito.mock(PredictionService.class);
        ObjectMapper mapper = new ObjectMapper().findAndRegisterModules();
        PredictionStreamHandler handler = new PredictionStreamHandler(service, mapper);

        WebSocketSession session = Mockito.mock(WebSocketSession.class);
        when(session.isOpen()).thenReturn(true);

        handler.afterConnectionEstablished(session);

        PredictionRecord record = new PredictionRecord("s1", 80.0, 0.2, Instant.now());
        handler.onApplicationEvent(new PredictionEvent(record));

        verify(session, times(1)).sendMessage(any(TextMessage.class));
    }

    @Test
    void sendsLatestOnConnect() throws Exception {
        PredictionRecord record = new PredictionRecord("s1", 72.0, 0.4, Instant.now());
        PredictionService service = Mockito.mock(PredictionService.class);
        when(service.getLatest()).thenReturn(record);

        ObjectMapper mapper = new ObjectMapper().findAndRegisterModules();
        PredictionStreamHandler handler = new PredictionStreamHandler(service, mapper);

        WebSocketSession session = Mockito.mock(WebSocketSession.class);
        when(session.isOpen()).thenReturn(true);

        handler.afterConnectionEstablished(session);

        verify(session, times(1)).sendMessage(any(TextMessage.class));
    }
}
