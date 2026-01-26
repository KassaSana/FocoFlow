package com.neurofocus.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.web.servlet.AutoConfigureMockMvc;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.MvcResult;
import org.springframework.test.annotation.DirtiesContext;

import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@SpringBootTest
@AutoConfigureMockMvc
@DirtiesContext(classMode = DirtiesContext.ClassMode.AFTER_EACH_TEST_METHOD)
class PredictionControllerTest {
    @Autowired
    private MockMvc mockMvc;

    @Autowired
    private ObjectMapper objectMapper;

    @Test
    void latestReturnsNotFoundWhenEmpty() throws Exception {
        mockMvc.perform(get("/api/predictions/latest"))
            .andExpect(status().isNotFound());
    }

    @Test
    void createAndFetchLatest() throws Exception {
        String payload = "{\"sessionId\":\"s1\",\"focusScore\":82.5,\"distractionRisk\":0.35}";
        MvcResult createResult = mockMvc.perform(
                post("/api/predictions")
                    .contentType(MediaType.APPLICATION_JSON)
                    .content(payload)
            )
            .andExpect(status().isOk())
            .andExpect(jsonPath("$.focusScore").value(82.5))
            .andExpect(jsonPath("$.distractionRisk").value(0.35))
            .andReturn();

        JsonNode createJson = objectMapper.readTree(createResult.getResponse().getContentAsString());
        String sessionId = createJson.get("sessionId").asText();

        mockMvc.perform(get("/api/predictions/latest"))
            .andExpect(status().isOk())
            .andExpect(jsonPath("$.sessionId").value(sessionId))
            .andExpect(jsonPath("$.focusScore").value(82.5))
            .andExpect(jsonPath("$.distractionRisk").value(0.35));
    }

    @Test
    void rejectsInvalidScores() throws Exception {
        String payload = "{\"sessionId\":\"s1\",\"focusScore\":120.0,\"distractionRisk\":1.2}";
        mockMvc.perform(
                post("/api/predictions")
                    .contentType(MediaType.APPLICATION_JSON)
                    .content(payload)
            )
            .andExpect(status().isBadRequest());
    }
}
