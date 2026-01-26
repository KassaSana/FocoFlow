package com.neurofocus.config;

import java.util.Arrays;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.CorsRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

@Configuration
public class CorsConfig implements WebMvcConfigurer {
    private static final String[] DEFAULT_PATTERNS = {
        "http://localhost:*",
        "http://127.0.0.1:*",
    };

    private final String[] allowedOriginPatterns;

    public CorsConfig(
        @Value("${neurofocus.allowed-origin-patterns:http://localhost:*,http://127.0.0.1:*}")
        String allowedOriginPatterns
    ) {
        this.allowedOriginPatterns = parsePatterns(allowedOriginPatterns);
    }

    @Override
    public void addCorsMappings(CorsRegistry registry) {
        registry.addMapping("/**")
            .allowedOriginPatterns(allowedOriginPatterns)
            .allowedMethods("GET", "POST", "PUT", "DELETE", "OPTIONS");
    }

    private static String[] parsePatterns(String raw) {
        if (raw == null || raw.trim().isEmpty()) {
            return DEFAULT_PATTERNS;
        }
        String[] patterns = Arrays.stream(raw.split(","))
            .map(String::trim)
            .filter(value -> !value.isEmpty())
            .toArray(String[]::new);
        if (patterns.length == 0) {
            return DEFAULT_PATTERNS;
        }
        return patterns;
    }
}
