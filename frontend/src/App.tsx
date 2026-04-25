import { useCallback, useEffect, useMemo, useRef, useState } from "react";

import {
  formatPercent,
  formatScore,
  formatTime,
  nextBackoffDelay,
  riskLabel,
  riskLevel,
} from "./utils";

type HealthStatus = "checking" | "online" | "offline";
type WsStatus = "offline" | "connecting" | "online" | "error" | "reconnecting";

type PredictionRecord = {
  sessionId: string;
  focusScore: number;
  distractionRisk: number;
  timestamp: string;
};

type SessionRecord = {
  sessionId: string;
  goal: string;
  status: string;
  startedAt: string | null;
  endedAt: string | null;
};

const STORAGE_KEY = "nf_api_base";
const DEFAULT_API_BASE =
  import.meta.env.VITE_API_BASE || "http://localhost:8080";
const HISTORY_LIMIT = 8;

const toWsUrl = (base: string) => {
  const url = new URL(base);
  const protocol = url.protocol === "https:" ? "wss:" : "ws:";
  return `${protocol}//${url.host}/ws/predictions`;
};

const parsePrediction = (data: unknown): PredictionRecord | null => {
  if (!data || typeof data !== "object") return null;
  const record = data as Partial<PredictionRecord>;
  if (typeof record.focusScore !== "number" || typeof record.distractionRisk !== "number") {
    return null;
  }
  return {
    sessionId: typeof record.sessionId === "string" ? record.sessionId : "",
    focusScore: record.focusScore,
    distractionRisk: record.distractionRisk,
    timestamp: typeof record.timestamp === "string" ? record.timestamp : "",
  };
};

const parseSession = (data: unknown): SessionRecord | null => {
  if (!data || typeof data !== "object") return null;
  const record = data as Partial<SessionRecord>;
  if (typeof record.sessionId !== "string" || typeof record.status !== "string") {
    return null;
  }
  return {
    sessionId: record.sessionId,
    goal: typeof record.goal === "string" ? record.goal : "",
    status: record.status,
    startedAt: typeof record.startedAt === "string" ? record.startedAt : null,
    endedAt: typeof record.endedAt === "string" ? record.endedAt : null,
  };
};

const buildSignals = (record: PredictionRecord | null) => {
  if (!record) {
    return ["Waiting for prediction stream."];
  }

  const level = riskLevel(record.distractionRisk);
  const signals = [`Risk level: ${level}`, `Focus score: ${formatScore(record.focusScore)}`];

  if (level === "high") {
    signals.push("Consider reducing app switches for the next 5 minutes.");
  } else if (level === "medium") {
    signals.push("Maintain steady typing rhythm to stay in flow.");
  } else if (level === "low") {
    signals.push("Focus is stable. Keep momentum.");
  } else {
    signals.push("Collecting context to refine the focus signal.");
  }

  return signals;
};

const readStoredApiBase = () => {
  if (typeof window === "undefined") return DEFAULT_API_BASE;
  return localStorage.getItem(STORAGE_KEY) || DEFAULT_API_BASE;
};

const storeApiBase = (value: string) => {
  if (typeof window === "undefined") return;
  localStorage.setItem(STORAGE_KEY, value);
};

export default function App() {
  const initialApiBase = readStoredApiBase();
  const [apiBase, setApiBase] = useState(initialApiBase);
  const [apiBaseInput, setApiBaseInput] = useState(initialApiBase);
  const [healthStatus, setHealthStatus] = useState<HealthStatus>("checking");
  const [wsStatus, setWsStatus] = useState<WsStatus>("offline");
  const [reconnectDelayMs, setReconnectDelayMs] = useState<number | null>(null);
  const [prediction, setPrediction] = useState<PredictionRecord | null>(null);
  const [predictionHistory, setPredictionHistory] = useState<PredictionRecord[]>([]);
  const [sessionGoal, setSessionGoal] = useState("");
  const [sessionRecord, setSessionRecord] = useState<SessionRecord | null>(null);
  const [sessionId, setSessionId] = useState<string | null>(null);
  const [apiBaseError, setApiBaseError] = useState<string | null>(null);
  const wsRef = useRef<WebSocket | null>(null);
  const reconnectTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const shouldReconnectRef = useRef(true);
  const retryCountRef = useRef(0);

  const pushPrediction = useCallback((record: PredictionRecord | null) => {
    if (!record) {
      setPrediction(null);
      return;
    }

    setPrediction(record);
    setPredictionHistory((current) => {
      const last = current[0];
      const isDuplicate =
        last &&
        last.timestamp === record.timestamp &&
        last.focusScore === record.focusScore &&
        last.distractionRisk === record.distractionRisk &&
        last.sessionId === record.sessionId;
      const next = isDuplicate ? current : [record, ...current];
      return next.slice(0, HISTORY_LIMIT);
    });
  }, []);

  const fetchHealth = useCallback(async () => {
    try {
      const response = await fetch(`${apiBase}/api/health`);
      if (!response.ok) throw new Error("Health error");
      setHealthStatus("online");
    } catch (err) {
      setHealthStatus("offline");
    }
  }, [apiBase, pushPrediction]);

  const fetchLatestPrediction = useCallback(async () => {
    try {
      const response = await fetch(`${apiBase}/api/predictions/latest`);
      if (!response.ok) {
        pushPrediction(null);
        return;
      }
      const data = await response.json();
      const record = parsePrediction(data);
      if (!record) {
        pushPrediction(null);
        return;
      }
      pushPrediction(record);
    } catch (err) {
      pushPrediction(null);
    }
  }, [apiBase, pushPrediction]);

  const fetchSession = useCallback(async () => {
    if (!sessionId) return;

    try {
      const response = await fetch(`${apiBase}/api/sessions/${sessionId}`);
      if (!response.ok) return;
      const record = parseSession(await response.json());
      if (!record) return;
      setSessionRecord(record);
      setSessionGoal((current) => (current ? current : record.goal));
    } catch (err) {
      // Ignore session refresh failures.
    }
  }, [apiBase, sessionId]);

  useEffect(() => {
    setHealthStatus("checking");
    void fetchHealth();
    void fetchLatestPrediction();
  }, [fetchHealth, fetchLatestPrediction]);

  useEffect(() => {
    if (!sessionId) {
      setSessionRecord(null);
      return;
    }

    void fetchSession();
    const intervalId = window.setInterval(() => {
      void fetchSession();
    }, 15000);

    return () => {
      window.clearInterval(intervalId);
    };
  }, [fetchSession, sessionId]);

  useEffect(() => {
    let isActive = true;
    shouldReconnectRef.current = true;
    retryCountRef.current = 0;

    const clearReconnectTimeout = () => {
      if (reconnectTimeoutRef.current) {
        clearTimeout(reconnectTimeoutRef.current);
        reconnectTimeoutRef.current = null;
      }
    };

    const connect = () => {
      clearReconnectTimeout();
      if (!isActive) return;

      if (wsRef.current) {
        wsRef.current.close();
      }

      let wsUrl: string;
      try {
        wsUrl = toWsUrl(apiBase);
      } catch (err) {
        setWsStatus("error");
        return;
      }

      const socket = new WebSocket(wsUrl);
      wsRef.current = socket;
      setReconnectDelayMs(null);
      setWsStatus(retryCountRef.current > 0 ? "reconnecting" : "connecting");

      socket.addEventListener("open", () => {
        if (!isActive) return;
        setWsStatus("online");
        setReconnectDelayMs(null);
        retryCountRef.current = 0;
      });
      socket.addEventListener("close", () => {
        if (!isActive) return;
        if (!shouldReconnectRef.current) {
          setWsStatus("offline");
          return;
        }
        const delay = nextBackoffDelay(retryCountRef.current);
        retryCountRef.current += 1;
        setReconnectDelayMs(delay);
        setWsStatus("reconnecting");
        reconnectTimeoutRef.current = setTimeout(connect, delay);
      });
      socket.addEventListener("error", () => {
        if (!isActive) return;
        setWsStatus("error");
      });
      socket.addEventListener("message", (event) => {
        try {
          const data = JSON.parse(event.data);
          const record = parsePrediction(data);
          if (record) {
            pushPrediction(record);
          }
        } catch (err) {
          // Ignore parse errors.
        }
      });
    };

    connect();

    return () => {
      isActive = false;
      shouldReconnectRef.current = false;
      clearReconnectTimeout();
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, [apiBase]);

  const handleApplyApiBase = () => {
    const trimmed = apiBaseInput.trim();
    const next = trimmed || apiBase;
    try {
      new URL(next);
    } catch (err) {
      setApiBaseError("Enter a valid URL, e.g. http://localhost:8080.");
      return;
    }
    setApiBaseError(null);
    setApiBase(next);
    setApiBaseInput(next);
    storeApiBase(next);
  };

  const handleSendTestPrediction = async () => {
    const payload = {
      sessionId: sessionId || "demo-session",
      focusScore: Math.random() * 100,
      distractionRisk: Math.random(),
    };

    try {
      await fetch(`${apiBase}/api/predictions`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });
    } catch (err) {
      // Ignore send failures.
    }
  };

  const handleStartSession = async () => {
    const goal = sessionGoal.trim();
    if (!goal) return;

    try {
      const response = await fetch(`${apiBase}/api/sessions/start`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ goal }),
      });
      if (!response.ok) return;
      const record = parseSession(await response.json());
      if (!record) return;
      setSessionRecord(record);
      setSessionId(record.sessionId);
      setSessionGoal(record.goal || goal);
    } catch (err) {
      // Ignore start failures.
    }
  };

  const handleStopSession = async () => {
    if (!sessionId) return;

    try {
      const response = await fetch(`${apiBase}/api/sessions/${sessionId}/stop`, {
        method: "POST",
      });
      if (!response.ok) return;
      const record = parseSession(await response.json());
      if (!record) return;
      setSessionRecord(record);
    } catch (err) {
      // Ignore stop failures.
    }
  };

  const signals = useMemo(() => buildSignals(prediction), [prediction]);
  const riskValue = prediction?.distractionRisk ?? null;
  const riskBadgeLabel = prediction ? riskLabel(riskValue) : "No data";
  const riskClass = riskLevel(riskValue);
  const sessionStatusLabel = sessionRecord ? sessionRecord.status.toLowerCase() : "idle";
  const predictionTimeLabel = prediction ? formatTime(prediction.timestamp) : "No data yet";
  const sessionStartedLabel = formatTime(sessionRecord?.startedAt ?? null);
  const sessionEndedLabel = formatTime(sessionRecord?.endedAt ?? null);
  const historyEntries = useMemo(() => predictionHistory, [predictionHistory]);

  const connectionNote = useMemo(() => {
    if (apiBaseError) return apiBaseError;
    if (healthStatus === "offline") {
      return "API offline. Start the backend or verify the base URL.";
    }
    if (wsStatus === "reconnecting" && reconnectDelayMs !== null) {
      const seconds = Math.max(1, Math.round(reconnectDelayMs / 1000));
      return `WebSocket reconnecting in ${seconds}s.`;
    }
    if (wsStatus === "error") {
      return "WebSocket error. Retrying connection.";
    }
    return null;
  }, [apiBaseError, healthStatus, reconnectDelayMs, wsStatus]);

  return (
    <div className="app">
      <header className="app-header">
        <div>
          <p className="eyebrow">FocoFlow</p>
          <h1>Live Focus Command Center</h1>
          <p className="subtitle">
            Predictive focus monitoring with real-time recovery cues and session flow.
          </p>
        </div>
        <div className="status-stack">
          <div className="status-pill">
            <span className="status-label">API</span>
            <span className="status-value">{healthStatus}</span>
          </div>
          <div className="status-pill">
            <span className="status-label">WebSocket</span>
            <span className="status-value">{wsStatus}</span>
          </div>
        </div>
      </header>

      <main className="grid">
        <section className="card live-card">
          <div className="card-header">
            <h2>Live Prediction</h2>
            <span className={`risk-badge risk-${riskClass}`}>{riskBadgeLabel}</span>
          </div>
          <div className="metrics">
            <div className="metric">
              <p className="metric-label">Focus score</p>
              <p className="metric-value">{formatScore(prediction?.focusScore ?? null)}</p>
            </div>
            <div className="metric">
              <p className="metric-label">Distraction risk</p>
              <p className="metric-value">{formatPercent(prediction?.distractionRisk ?? null)}</p>
            </div>
          </div>
          <div className="meta">
            <div>
              <p className="meta-label">Last update</p>
              <p className="meta-value">{predictionTimeLabel}</p>
            </div>
            <div>
              <p className="meta-label">Session</p>
              <p className="meta-value">{prediction?.sessionId || "--"}</p>
            </div>
          </div>
          <button className="ghost-button" onClick={handleSendTestPrediction}>
            Send sample prediction
          </button>
        </section>

        <section className="card session-card">
          <div className="card-header">
            <h2>Session Control</h2>
            <span className="session-status">{sessionStatusLabel}</span>
          </div>
          <label className="field">
            <span>Focus goal</span>
            <input
              type="text"
              placeholder="Ship the focus engine"
              value={sessionGoal}
              onChange={(event) => setSessionGoal(event.target.value)}
            />
          </label>
          <div className="button-row">
            <button className="primary-button" onClick={handleStartSession}>
              Start session
            </button>
            <button className="secondary-button" onClick={handleStopSession}>
              Stop session
            </button>
          </div>
          <div className="meta">
            <div>
              <p className="meta-label">Session ID</p>
              <p className="meta-value">{sessionId || "--"}</p>
            </div>
            <div>
              <p className="meta-label">Started</p>
              <p className="meta-value">{sessionStartedLabel}</p>
            </div>
            <div>
              <p className="meta-label">Ended</p>
              <p className="meta-value">{sessionEndedLabel}</p>
            </div>
          </div>
        </section>

        <section className="card signals-card">
          <div className="card-header">
            <h2>Signals</h2>
            <span className="pill">rolling 30s</span>
          </div>
          <ul className="signal-list">
            {signals.map((signal, index) => (
              <li key={`${signal}-${index}`}>{signal}</li>
            ))}
          </ul>
        </section>

        <section className="card history-card">
          <div className="card-header">
            <h2>Recent Predictions</h2>
            <span className="pill">latest {HISTORY_LIMIT}</span>
          </div>
          <ul className="history-list">
            {historyEntries.length === 0 ? (
              <li className="history-empty">No predictions yet.</li>
            ) : (
              historyEntries.map((entry) => (
                <li
                  key={`${entry.timestamp}-${entry.sessionId}-${entry.focusScore}`}
                  className="history-item"
                >
                  <div>
                    <p className="history-time">{formatTime(entry.timestamp)}</p>
                    <p className="history-session">{entry.sessionId || "unknown session"}</p>
                  </div>
                  <div className="history-metrics">
                    <span className="history-score">{formatScore(entry.focusScore)}</span>
                    <span className={`history-risk risk-${riskLevel(entry.distractionRisk)}`}>
                      {formatPercent(entry.distractionRisk)}
                    </span>
                  </div>
                </li>
              ))
            )}
          </ul>
        </section>

        <section className="card config-card">
          <div className="card-header">
            <h2>Connection</h2>
            <span className="pill">local dev</span>
          </div>
          <label className="field">
            <span>API base URL</span>
            <input
              type="text"
              value={apiBaseInput}
              onChange={(event) => {
                setApiBaseInput(event.target.value);
                if (apiBaseError) setApiBaseError(null);
              }}
            />
          </label>
          <button className="secondary-button" onClick={handleApplyApiBase}>
            Apply
          </button>
          {connectionNote ? <p className="helper-text">{connectionNote}</p> : null}
          <p className="helper-text">
            WebSocket endpoint expected at <code>/ws/predictions</code>.
          </p>
        </section>
      </main>
    </div>
  );
}
