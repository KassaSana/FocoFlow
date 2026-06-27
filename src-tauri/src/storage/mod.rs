use std::path::PathBuf;

use rusqlite::{params, Connection};
use thiserror::Error;
use uuid::Uuid;

use crate::types::{
    ContextSnapshotDto, FocusLabel, PredictionRecord, SessionRecap, SessionRecord,
};

#[derive(Debug, Error)]
pub enum StorageError {
    #[error("database error: {0}")]
    Sqlite(#[from] rusqlite::Error),
    #[error("session not found")]
    SessionNotFound,
}

pub struct Storage {
    conn: Connection,
}

impl Storage {
    pub fn open(app_data_dir: PathBuf) -> Result<Self, StorageError> {
        std::fs::create_dir_all(&app_data_dir).ok();
        let db_path = app_data_dir.join("focoflow.db");
        let conn = Connection::open(db_path)?;
        let storage = Self { conn };
        storage.init_schema()?;
        Ok(storage)
    }

    fn init_schema(&self) -> Result<(), StorageError> {
        self.conn.execute_batch(
            "
            CREATE TABLE IF NOT EXISTS sessions (
                session_id TEXT PRIMARY KEY,
                goal TEXT NOT NULL,
                status TEXT NOT NULL,
                focus_mode TEXT NOT NULL DEFAULT 'normal',
                started_at TEXT NOT NULL,
                ended_at TEXT
            );

            CREATE TABLE IF NOT EXISTS predictions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                focus_score REAL NOT NULL,
                distraction_risk REAL NOT NULL,
                focus_state TEXT NOT NULL,
                timestamp TEXT NOT NULL,
                FOREIGN KEY (session_id) REFERENCES sessions(session_id)
            );

            CREATE INDEX IF NOT EXISTS idx_predictions_session_ts
                ON predictions(session_id, timestamp DESC);

            CREATE TABLE IF NOT EXISTS context_snapshots (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                app_name TEXT NOT NULL,
                window_title TEXT NOT NULL,
                file_hint TEXT NOT NULL,
                project_hint TEXT NOT NULL,
                summary TEXT NOT NULL,
                timestamp TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS labels (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                label INTEGER NOT NULL,
                source TEXT NOT NULL DEFAULT 'manual',
                notes TEXT,
                timestamp TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS snapback_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL,
                summary TEXT NOT NULL,
                timestamp TEXT NOT NULL
            );
            ",
        )?;
        Ok(())
    }

    pub fn start_session(
        &self,
        goal: &str,
        focus_mode: &str,
    ) -> Result<SessionRecord, StorageError> {
        let session_id = Uuid::new_v4().to_string();
        let started_at = chrono::Utc::now().to_rfc3339();
        self.conn.execute(
            "INSERT INTO sessions (session_id, goal, status, focus_mode, started_at) VALUES (?1, ?2, 'ACTIVE', ?3, ?4)",
            params![session_id, goal, focus_mode, started_at],
        )?;
        Ok(SessionRecord {
            session_id,
            goal: goal.to_string(),
            status: "ACTIVE".to_string(),
            focus_mode: focus_mode.to_string(),
            started_at: Some(started_at),
            ended_at: None,
        })
    }

    pub fn stop_session(&self, session_id: &str) -> Result<SessionRecord, StorageError> {
        if let Ok(existing) = self.get_session(session_id) {
            if existing.status == "COMPLETED" {
                return Ok(existing);
            }
        }

        let ended_at = chrono::Utc::now().to_rfc3339();
        let updated = self.conn.execute(
            "UPDATE sessions SET status = 'COMPLETED', ended_at = ?1 WHERE session_id = ?2 AND status = 'ACTIVE'",
            params![ended_at, session_id],
        )?;
        if updated == 0 {
            return Err(StorageError::SessionNotFound);
        }
        self.get_session(session_id)
    }

    pub fn get_session(&self, session_id: &str) -> Result<SessionRecord, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, goal, status, focus_mode, started_at, ended_at FROM sessions WHERE session_id = ?1",
        )?;
        let row = stmt.query_row(params![session_id], |row| {
            Ok(SessionRecord {
                session_id: row.get(0)?,
                goal: row.get(1)?,
                status: row.get(2)?,
                focus_mode: row.get(3)?,
                started_at: row.get(4)?,
                ended_at: row.get(5)?,
            })
        })?;
        Ok(row)
    }

    pub fn get_active_session(&self) -> Result<Option<SessionRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, goal, status, focus_mode, started_at, ended_at FROM sessions WHERE status = 'ACTIVE' ORDER BY started_at DESC LIMIT 1",
        )?;
        let mut rows = stmt.query([])?;
        if let Some(row) = rows.next()? {
            Ok(Some(SessionRecord {
                session_id: row.get(0)?,
                goal: row.get(1)?,
                status: row.get(2)?,
                focus_mode: row.get(3)?,
                started_at: row.get(4)?,
                ended_at: row.get(5)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn save_prediction(&self, record: &PredictionRecord) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO predictions (session_id, focus_score, distraction_risk, focus_state, timestamp) VALUES (?1, ?2, ?3, ?4, ?5)",
            params![
                record.session_id,
                record.focus_score,
                record.distraction_risk,
                record.focus_state,
                record.timestamp,
            ],
        )?;
        Ok(())
    }

    pub fn latest_prediction(&self) -> Result<Option<PredictionRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, focus_score, distraction_risk, focus_state, timestamp FROM predictions ORDER BY timestamp DESC LIMIT 1",
        )?;
        let mut rows = stmt.query([])?;
        if let Some(row) = rows.next()? {
            Ok(Some(PredictionRecord {
                session_id: row.get(0)?,
                focus_score: row.get(1)?,
                distraction_risk: row.get(2)?,
                focus_state: row.get(3)?,
                timestamp: row.get(4)?,
            }))
        } else {
            Ok(None)
        }
    }

    pub fn recent_predictions(&self, limit: usize) -> Result<Vec<PredictionRecord>, StorageError> {
        let mut stmt = self.conn.prepare(
            "SELECT session_id, focus_score, distraction_risk, focus_state, timestamp FROM predictions ORDER BY timestamp DESC LIMIT ?1",
        )?;
        let rows = stmt.query_map(params![limit as i64], |row| {
            Ok(PredictionRecord {
                session_id: row.get(0)?,
                focus_score: row.get(1)?,
                distraction_risk: row.get(2)?,
                focus_state: row.get(3)?,
                timestamp: row.get(4)?,
            })
        })?;
        Ok(rows.filter_map(Result::ok).collect())
    }

    pub fn save_context_snapshot(
        &self,
        session_id: &str,
        snapshot: &ContextSnapshotDto,
    ) -> Result<(), StorageError> {
        self.conn.execute(
            "INSERT INTO context_snapshots (session_id, app_name, window_title, file_hint, project_hint, summary, timestamp) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)",
            params![
                session_id,
                snapshot.app_name,
                snapshot.window_title,
                snapshot.file_hint,
                snapshot.project_hint,
                snapshot.summary,
                snapshot.timestamp,
            ],
        )?;
        Ok(())
    }

    pub fn save_label(
        &self,
        session_id: &str,
        label: FocusLabel,
        notes: Option<&str>,
    ) -> Result<(), StorageError> {
        let timestamp = chrono::Utc::now().to_rfc3339();
        self.conn.execute(
            "INSERT INTO labels (session_id, label, source, notes, timestamp) VALUES (?1, ?2, 'manual', ?3, ?4)",
            params![session_id, label as i32, notes, timestamp],
        )?;
        Ok(())
    }

    pub fn record_snapback(&self, session_id: &str, summary: &str) -> Result<(), StorageError> {
        let timestamp = chrono::Utc::now().to_rfc3339();
        self.conn.execute(
            "INSERT INTO snapback_events (session_id, summary, timestamp) VALUES (?1, ?2, ?3)",
            params![session_id, summary, timestamp],
        )?;
        Ok(())
    }

    pub fn session_recap(&self, session_id: &str) -> Result<SessionRecap, StorageError> {
        let session = self.get_session(session_id)?;
        let started = session
            .started_at
            .as_deref()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
            .map(|d| d.with_timezone(&chrono::Utc));
        let ended = session
            .ended_at
            .as_deref()
            .and_then(|s| chrono::DateTime::parse_from_rfc3339(s).ok())
            .map(|d| d.with_timezone(&chrono::Utc))
            .unwrap_or_else(chrono::Utc::now);
        let duration_secs = started
            .map(|s| (ended - s).num_seconds().max(0) as u64)
            .unwrap_or(0);

        let (avg_focus, avg_risk, deep_pct): (f64, f64, f64) = self.conn.query_row(
            "SELECT COALESCE(AVG(focus_score), 0), COALESCE(AVG(distraction_risk), 0),
                    COALESCE(100.0 * SUM(CASE WHEN focus_state = 'DEEP_FOCUS' THEN 1 ELSE 0 END) / NULLIF(COUNT(*), 0), 0)
             FROM predictions WHERE session_id = ?1",
            params![session_id],
            |row| Ok((row.get(0)?, row.get(1)?, row.get(2)?)),
        )?;

        let snapback_count: u32 = self.conn.query_row(
            "SELECT COUNT(*) FROM snapback_events WHERE session_id = ?1",
            params![session_id],
            |row| row.get(0),
        )?;

        let thrash_spikes: u32 = self.conn.query_row(
            "SELECT COUNT(*) FROM predictions WHERE session_id = ?1 AND distraction_risk >= 0.7 AND focus_state = 'DISTRACTED'",
            params![session_id],
            |row| row.get(0),
        )?;

        Ok(SessionRecap {
            session_id: session_id.to_string(),
            goal: session.goal,
            duration_secs,
            avg_focus_score: avg_focus,
            avg_distraction_risk: avg_risk,
            snapback_count,
            thrash_spikes,
            deep_focus_pct: deep_pct,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn session_lifecycle() {
        let dir = std::env::temp_dir().join(format!("focoflow_test_{}", Uuid::new_v4()));
        let storage = Storage::open(dir).unwrap();
        let session = storage.start_session("Ship snapback", "normal").unwrap();
        assert_eq!(session.status, "ACTIVE");
        let stopped = storage.stop_session(&session.session_id).unwrap();
        assert_eq!(stopped.status, "COMPLETED");
    }
}
