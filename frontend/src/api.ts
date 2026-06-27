import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

export type RiskLevel = "high" | "medium" | "low" | "unknown";

export type PredictionRecord = {
  sessionId: string;
  focusScore: number;
  distractionRisk: number;
  focusState: string;
  timestamp: string;
};

export type SessionRecord = {
  sessionId: string;
  goal: string;
  status: string;
  focusMode: string;
  startedAt: string | null;
  endedAt: string | null;
};

export type PermissionStatus = {
  captureAvailable: boolean;
  activeWindowAvailable: boolean;
  message: string;
};

export type HealthStatus = {
  status: string;
  captureRunning: boolean;
  permissions: PermissionStatus;
};

export type SessionRecap = {
  sessionId: string;
  goal: string;
  durationSecs: number;
  avgFocusScore: number;
  avgDistractionRisk: number;
  snapbackCount: number;
  thrashSpikes: number;
  deepFocusPct: number;
};

export type FocusLabel =
  | "DISTRACTED"
  | "PSEUDO_PRODUCTIVE"
  | "PRODUCTIVE"
  | "DEEP_FOCUS";

function mapPrediction(raw: Record<string, unknown>): PredictionRecord {
  return {
    sessionId: String(raw.session_id ?? raw.sessionId ?? ""),
    focusScore: Number(raw.focus_score ?? raw.focusScore ?? 0),
    distractionRisk: Number(raw.distraction_risk ?? raw.distractionRisk ?? 0),
    focusState: String(raw.focus_state ?? raw.focusState ?? "UNKNOWN"),
    timestamp: String(raw.timestamp ?? ""),
  };
}

function mapSession(raw: Record<string, unknown>): SessionRecord {
  return {
    sessionId: String(raw.session_id ?? raw.sessionId ?? ""),
    goal: String(raw.goal ?? ""),
    status: String(raw.status ?? ""),
    focusMode: String(raw.focus_mode ?? raw.focusMode ?? "normal"),
    startedAt: (raw.started_at ?? raw.startedAt ?? null) as string | null,
    endedAt: (raw.ended_at ?? raw.endedAt ?? null) as string | null,
  };
}

export const api = {
  getHealth: () => invoke<HealthStatus>("get_health"),
  getLatestPrediction: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_latest_prediction");
    return raw ? mapPrediction(raw) : null;
  },
  getPredictionHistory: async (limit = 8) => {
    const rows = await invoke<Record<string, unknown>[]>("get_prediction_history", { limit });
    return rows.map(mapPrediction);
  },
  startSession: async (goal: string, focusMode = "normal") => {
    const raw = await invoke<Record<string, unknown>>("start_session", { goal, focusMode });
    return mapSession(raw);
  },
  stopSession: async (sessionId: string) => {
    const raw = await invoke<Record<string, unknown>>("stop_session", { sessionId });
    return mapSession(raw);
  },
  getSession: async (sessionId: string) => {
    const raw = await invoke<Record<string, unknown>>("get_session", { sessionId });
    return mapSession(raw);
  },
  getActiveSession: async () => {
    const raw = await invoke<Record<string, unknown> | null>("get_active_session");
    return raw ? mapSession(raw) : null;
  },
  submitLabel: (sessionId: string, label: FocusLabel, notes?: string) =>
    invoke("submit_label", { request: { sessionId, label, notes } }),
  getSessionRecap: async (sessionId: string) => {
    const raw = await invoke<Record<string, unknown>>("get_session_recap", { sessionId });
    return {
      sessionId: String(raw.session_id ?? raw.sessionId ?? ""),
      goal: String(raw.goal ?? ""),
      durationSecs: Number(raw.duration_secs ?? raw.durationSecs ?? 0),
      avgFocusScore: Number(raw.avg_focus_score ?? raw.avgFocusScore ?? 0),
      avgDistractionRisk: Number(raw.avg_distraction_risk ?? raw.avgDistractionRisk ?? 0),
      snapbackCount: Number(raw.snapback_count ?? raw.snapbackCount ?? 0),
      thrashSpikes: Number(raw.thrash_spikes ?? raw.thrashSpikes ?? 0),
      deepFocusPct: Number(raw.deep_focus_pct ?? raw.deepFocusPct ?? 0),
    } satisfies SessionRecap;
  },
  setFocusMode: (mode: string) => invoke("set_focus_mode", { mode }),
  sendTestPrediction: async () => {
    const raw = await invoke<Record<string, unknown>>("send_test_prediction");
    return mapPrediction(raw);
  },
  refreshPermissions: () => invoke<PermissionStatus>("refresh_permissions"),
  onPrediction: (handler: (record: PredictionRecord) => void) =>
    listen<Record<string, unknown>>("prediction", (event) => {
      handler(mapPrediction(event.payload));
    }),
  onSnapback: (handler: (payload: Record<string, unknown>) => void) =>
    listen<Record<string, unknown>>("snapback", (event) => handler(event.payload)),
  onHyperfocus: (handler: (payload: { message: string }) => void) =>
    listen<{ message: string }>("hyperfocus", (event) => handler(event.payload)),
};

export const clamp = (value: number, min: number, max: number) =>
  Math.min(Math.max(value, min), max);

export const formatPercent = (value: number | null | undefined) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  const pct = clamp(value, 0, 1) * 100;
  return `${pct.toFixed(1)}%`;
};

export const formatScore = (value: number | null | undefined) => {
  if (value === null || value === undefined || Number.isNaN(value)) return "--";
  const score = clamp(value, 0, 100);
  return score.toFixed(1);
};

export const formatTime = (isoString: string | null | undefined) => {
  if (!isoString) return "--";
  const date = new Date(isoString);
  if (Number.isNaN(date.getTime())) return "--";
  return date.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
};

export const riskLevel = (risk: number | null | undefined): RiskLevel => {
  if (risk === null || risk === undefined || Number.isNaN(risk)) return "unknown";
  if (risk >= 0.7) return "high";
  if (risk >= 0.4) return "medium";
  return "low";
};

export const riskLabel = (risk: number | null | undefined) => {
  const level = riskLevel(risk);
  if (level === "high") return "High risk";
  if (level === "medium") return "Medium risk";
  if (level === "low") return "Low risk";
  return "Unknown";
};

export const focusStateLabel = (state: string | null | undefined) => {
  switch (state) {
    case "DEEP_FOCUS":
      return "Deep work";
    case "PRODUCTIVE":
      return "Productive";
    case "PSEUDO_PRODUCTIVE":
      return "Drift";
    case "DISTRACTED":
      return "Distracted";
    default:
      return "Unknown";
  }
};

export const nextBackoffDelay = (attempt: number) => {
  const safeAttempt = Math.max(0, attempt);
  const baseMs = 500;
  const maxMs = 10000;
  const delay = baseMs * Math.pow(2, safeAttempt);
  return Math.min(delay, maxMs);
};
