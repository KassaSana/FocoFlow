import { useCallback, useEffect, useMemo, useState } from "react";

import {
  api,
  focusStateLabel,
  formatPercent,
  formatScore,
  formatTime,
  riskLabel,
  riskLevel,
  type AppRuleKind,
  type AppRuleRecord,
  type FocusLabel,
  type PredictionRecord,
  type SessionRecord,
  type SessionRecap,
} from "./api";

const HISTORY_LIMIT = 8;
const FOCUS_MODES = ["deep", "normal", "recovery"] as const;
const APP_RULE_KINDS: AppRuleKind[] = ["allow", "block"];

const ruleKindLabel = (kind: AppRuleKind) => (kind === "allow" ? "Allow" : "Block");

const buildSignals = (record: PredictionRecord | null) => {
  if (!record) {
    return ["Waiting for live capture."];
  }

  const level = riskLevel(record.distractionRisk);
  const signals = [
    `Focus state: ${focusStateLabel(record.focusState)}`,
    `Thrash: ${(record.thrashScore * 100).toFixed(0)}% · Drift: ${(record.driftScore * 100).toFixed(0)}% · Goal fit: ${(record.goalAlignment * 100).toFixed(0)}%`,
    `Risk level: ${level}`,
    `Focus score: ${formatScore(record.focusScore)}`,
  ];

  if (record.focusState === "PSEUDO_PRODUCTIVE") {
    signals.push("Drift detected — tab/title churn or scattered typing in a work app.");
  } else if (record.thrashScore >= 0.6) {
    signals.push("Context-switch thrash — jumping between apps/windows rapidly.");
  } else if (record.focusState === "DEEP_FOCUS") {
    signals.push("Deep work detected. Hyperfocus guardrail is watching.");
  } else if (level === "low") {
    signals.push("Focus is stable. Keep momentum.");
  }

  return signals;
};

export default function App() {
  const [healthStatus, setHealthStatus] = useState<"checking" | "online" | "offline">("checking");
  const [captureRunning, setCaptureRunning] = useState(false);
  const [permissionMessage, setPermissionMessage] = useState<string | null>(null);
  const [prediction, setPrediction] = useState<PredictionRecord | null>(null);
  const [predictionHistory, setPredictionHistory] = useState<PredictionRecord[]>([]);
  const [sessionGoal, setSessionGoal] = useState("");
  const [sessionRecord, setSessionRecord] = useState<SessionRecord | null>(null);
  const [sessionId, setSessionId] = useState<string | null>(null);
  const [focusMode, setFocusMode] = useState<(typeof FOCUS_MODES)[number]>("normal");
  const [recap, setRecap] = useState<SessionRecap | null>(null);
  const [hyperfocusNote, setHyperfocusNote] = useState<string | null>(null);
  const [snapbackNote, setSnapbackNote] = useState<string | null>(null);
  const [labelStatus, setLabelStatus] = useState<string | null>(null);
  const [appRules, setAppRules] = useState<AppRuleRecord[]>([]);
  const [rulePattern, setRulePattern] = useState("");
  const [ruleKind, setRuleKind] = useState<AppRuleKind>("allow");
  const [ruleNote, setRuleNote] = useState("");
  const [rulesStatus, setRulesStatus] = useState<string | null>(null);

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
        last.distractionRisk === record.distractionRisk;
      const next = isDuplicate ? current : [record, ...current];
      return next.slice(0, HISTORY_LIMIT);
    });
  }, []);

  const refreshAppRules = useCallback(async () => {
    try {
      const rules = await api.getAppRules();
      setAppRules(rules);
    } catch {
      setRulesStatus("Could not load app rules.");
    }
  }, []);

  const refreshHealth = useCallback(async () => {
    try {
      const health = await api.getHealth();
      setHealthStatus("online");
      setCaptureRunning(health.captureRunning);
      setPermissionMessage(health.permissions.message);
    } catch {
      setHealthStatus("offline");
    }
  }, []);

  const refreshLatest = useCallback(async () => {
    try {
      const latest = await api.getLatestPrediction();
      pushPrediction(latest);
      const history = await api.getPredictionHistory(HISTORY_LIMIT);
      if (history.length > 0) {
        setPredictionHistory(history);
      }
    } catch {
      pushPrediction(null);
    }
  }, [pushPrediction]);

  useEffect(() => {
    void refreshHealth();
    void refreshLatest();
    void refreshAppRules();
    void api.getActiveSession().then((active) => {
      if (!active) return;
      setSessionRecord(active);
      setSessionId(active.sessionId);
      setSessionGoal(active.goal);
      setFocusMode((active.focusMode as (typeof FOCUS_MODES)[number]) || "normal");
    });
  }, [refreshHealth, refreshLatest, refreshAppRules]);

  useEffect(() => {
    const unsubs: Array<Promise<() => void>> = [];
    unsubs.push(
      api.onPrediction((record) => {
        pushPrediction(record);
      }),
    );
    unsubs.push(
      api.onSnapback((payload) => {
        const summary = String(payload.summary ?? "Previous task");
        setSnapbackNote(`Snapback: ${summary}`);
      }),
    );
    unsubs.push(
      api.onHyperfocus((payload) => {
        setHyperfocusNote(payload.message);
      }),
    );

    return () => {
      void Promise.all(unsubs).then((handlers) => handlers.forEach((off) => off()));
    };
  }, [pushPrediction]);

  const handleStartSession = async () => {
    const goal = sessionGoal.trim();
    if (!goal) return;
    try {
      const record = await api.startSession(goal, focusMode);
      setSessionRecord(record);
      setSessionId(record.sessionId);
      setSessionGoal(record.goal);
      setRecap(null);
    } catch {
      // ignore
    }
  };

  const handleStopSession = async () => {
    if (!sessionId) return;
    try {
      const record = await api.stopSession(sessionId);
      setSessionRecord(record);
      const sessionRecap = await api.getSessionRecap(sessionId);
      setRecap(sessionRecap);
    } catch {
      // ignore
    }
  };

  const handleFocusModeChange = async (mode: (typeof FOCUS_MODES)[number]) => {
    setFocusMode(mode);
    try {
      await api.setFocusMode(mode);
    } catch {
      // ignore
    }
  };

  const handleLabel = async (label: FocusLabel) => {
    if (!sessionId) {
      setLabelStatus("Start a session to save feedback.");
      return;
    }
    try {
      await api.submitLabel(sessionId, label);
      setLabelStatus(`Saved: ${focusStateLabel(label)}`);
    } catch {
      setLabelStatus("Could not save feedback.");
    }
  };

  const handleSendTestPrediction = async () => {
    try {
      const record = await api.sendTestPrediction();
      pushPrediction(record);
    } catch {
      // ignore
    }
  };

  const handleRefreshPermissions = async () => {
    try {
      const status = await api.refreshPermissions();
      setPermissionMessage(status.message);
    } catch {
      // ignore
    }
  };

  const handleAddAppRule = async () => {
    const pattern = rulePattern.trim();
    if (!pattern) {
      setRulesStatus("Enter an app name or keyword (e.g. discord, notion).");
      return;
    }

    try {
      const saved = await api.upsertAppRule(pattern, ruleKind, ruleNote.trim() || undefined);
      await refreshAppRules();
      setRulePattern("");
      setRuleNote("");
      setRulesStatus(`Saved ${ruleKindLabel(saved.ruleType).toLowerCase()} rule for "${saved.pattern}".`);
    } catch {
      setRulesStatus("Could not save app rule.");
    }
  };

  const handleDeleteAppRule = async (rule: AppRuleRecord) => {
    try {
      await api.deleteAppRule(rule.id);
      setAppRules((current) => current.filter((entry) => entry.id !== rule.id));
      setRulesStatus(`Removed rule for "${rule.pattern}".`);
    } catch {
      setRulesStatus("Could not delete app rule.");
    }
  };

  const signals = useMemo(() => buildSignals(prediction), [prediction]);
  const riskValue = prediction?.distractionRisk ?? null;
  const riskBadgeLabel = prediction ? riskLabel(riskValue) : "No data";
  const riskClass = riskLevel(riskValue);
  const sessionStatusLabel = sessionRecord ? sessionRecord.status.toLowerCase() : "idle";

  return (
    <div className="app">
      <header className="app-header">
        <div>
          <p className="eyebrow">Snapback</p>
          <h1>Live Focus Command Center</h1>
          <p className="subtitle">
            Measures how you work — deep focus, drift, and context-switch thrash — with snapback
            recovery when you return.
          </p>
        </div>
        <div className="status-stack">
          <div className="status-pill">
            <span className="status-label">App</span>
            <span className="status-value">{healthStatus}</span>
          </div>
          <div className="status-pill">
            <span className="status-label">Capture</span>
            <span className="status-value">{captureRunning ? "running" : "idle"}</span>
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
            <div className="metric">
              <p className="metric-label">State</p>
              <p className="metric-value state-value">
                {focusStateLabel(prediction?.focusState ?? null)}
              </p>
            </div>
          </div>
          <div className="meta">
            <div>
              <p className="meta-label">Last update</p>
              <p className="meta-value">{formatTime(prediction?.timestamp ?? null)}</p>
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
              placeholder="Ship the snapback overlay"
              value={sessionGoal}
              onChange={(event) => setSessionGoal(event.target.value)}
            />
          </label>
          <label className="field">
            <span>Focus mode</span>
            <select
              value={focusMode}
              onChange={(event) =>
                void handleFocusModeChange(event.target.value as (typeof FOCUS_MODES)[number])
              }
            >
              {FOCUS_MODES.map((mode) => (
                <option key={mode} value={mode}>
                  {mode}
                </option>
              ))}
            </select>
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
              <p className="meta-value">{formatTime(sessionRecord?.startedAt ?? null)}</p>
            </div>
            <div>
              <p className="meta-label">Ended</p>
              <p className="meta-value">{formatTime(sessionRecord?.endedAt ?? null)}</p>
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
          {hyperfocusNote ? <p className="helper-text alert">{hyperfocusNote}</p> : null}
          {snapbackNote ? <p className="helper-text snapback">{snapbackNote}</p> : null}
        </section>

        <section className="card feedback-card">
          <div className="card-header">
            <h2>Focus Feedback</h2>
            <span className="pill">train the model</span>
          </div>
          <p className="helper-text">One tap — was that moment actually focused?</p>
          <div className="button-row feedback-row">
            <button className="secondary-button" onClick={() => void handleLabel("DEEP_FOCUS")}>
              Deep
            </button>
            <button className="secondary-button" onClick={() => void handleLabel("PRODUCTIVE")}>
              Focused
            </button>
            <button
              className="secondary-button"
              onClick={() => void handleLabel("PSEUDO_PRODUCTIVE")}
            >
              Drift
            </button>
            <button className="secondary-button" onClick={() => void handleLabel("DISTRACTED")}>
              Distracted
            </button>
          </div>
          {labelStatus ? <p className="helper-text">{labelStatus}</p> : null}
        </section>

        <section className="card history-card">
          <div className="card-header">
            <h2>Recent Predictions</h2>
            <span className="pill">latest {HISTORY_LIMIT}</span>
          </div>
          <ul className="history-list">
            {predictionHistory.length === 0 ? (
              <li className="history-empty">No predictions yet.</li>
            ) : (
              predictionHistory.map((entry) => (
                <li
                  key={`${entry.timestamp}-${entry.sessionId}-${entry.focusScore}`}
                  className="history-item"
                >
                  <div>
                    <p className="history-time">{formatTime(entry.timestamp)}</p>
                    <p className="history-session">{focusStateLabel(entry.focusState)}</p>
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

        {recap ? (
          <section className="card recap-card">
            <div className="card-header">
              <h2>Session Recap</h2>
              <span className="pill">summary</span>
            </div>
            <div className="meta">
              <div>
                <p className="meta-label">Duration</p>
                <p className="meta-value">{Math.round(recap.durationSecs / 60)} min</p>
              </div>
              <div>
                <p className="meta-label">Avg focus</p>
                <p className="meta-value">{formatScore(recap.avgFocusScore)}</p>
              </div>
              <div>
                <p className="meta-label">Deep work</p>
                <p className="meta-value">{recap.deepFocusPct.toFixed(0)}%</p>
              </div>
              <div>
                <p className="meta-label">Snapbacks</p>
                <p className="meta-value">{recap.snapbackCount}</p>
              </div>
              <div>
                <p className="meta-label">Thrash spikes</p>
                <p className="meta-value">{recap.thrashSpikes}</p>
              </div>
            </div>
          </section>
        ) : null}

        <section className="card rules-card">
          <div className="card-header">
            <h2>Personal App Rules</h2>
            <span className="pill">your overrides</span>
          </div>
          <p className="helper-text">
            Match part of an app name or window title. Allow marks it as on-task for you; Block
            treats it as a distraction.
          </p>
          <label className="field">
            <span>Pattern</span>
            <input
              type="text"
              placeholder="discord, notion, youtube"
              value={rulePattern}
              onChange={(event) => setRulePattern(event.target.value)}
            />
          </label>
          <label className="field">
            <span>Rule type</span>
            <select
              value={ruleKind}
              onChange={(event) => setRuleKind(event.target.value as AppRuleKind)}
            >
              {APP_RULE_KINDS.map((kind) => (
                <option key={kind} value={kind}>
                  {ruleKindLabel(kind)}
                </option>
              ))}
            </select>
          </label>
          <label className="field">
            <span>Note (optional)</span>
            <input
              type="text"
              placeholder="study group server"
              value={ruleNote}
              onChange={(event) => setRuleNote(event.target.value)}
            />
          </label>
          <button className="primary-button" onClick={() => void handleAddAppRule()}>
            Save rule
          </button>
          <ul className="rules-list">
            {appRules.length === 0 ? (
              <li className="rules-empty">No personal rules yet.</li>
            ) : (
              appRules.map((rule) => (
                <li key={rule.id} className="rules-item">
                  <div>
                    <div className="rules-item-header">
                      <span className="rules-pattern">{rule.pattern}</span>
                      <span className={`rules-badge rules-badge-${rule.ruleType}`}>
                        {ruleKindLabel(rule.ruleType)}
                      </span>
                    </div>
                    {rule.note ? <p className="rules-note">{rule.note}</p> : null}
                  </div>
                  <button
                    className="secondary-button rules-delete"
                    onClick={() => void handleDeleteAppRule(rule)}
                  >
                    Remove
                  </button>
                </li>
              ))
            )}
          </ul>
          {rulesStatus ? <p className="helper-text">{rulesStatus}</p> : null}
        </section>

        <section className="card config-card">
          <div className="card-header">
            <h2>Permissions</h2>
            <span className="pill">local desktop</span>
          </div>
          <p className="helper-text">
            {permissionMessage ||
              "Snapback runs locally. Grant Accessibility + Input Monitoring on macOS."}
          </p>
          <button className="secondary-button" onClick={handleRefreshPermissions}>
            Refresh permissions
          </button>
        </section>
      </main>
    </div>
  );
}
