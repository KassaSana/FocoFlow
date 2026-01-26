package com.neurofocus.websocket;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {
    private final PredictionStreamHandler predictionStreamHandler;

    public WebSocketConfig(PredictionStreamHandler predictionStreamHandler) {
        this.predictionStreamHandler = predictionStreamHandler;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(predictionStreamHandler, "/ws/predictions").setAllowedOrigins("*");
    }
}
