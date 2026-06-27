use std::thread;

use tauri::{AppHandle, Emitter, Manager};

use crate::engine::{check_hyperfocus, Classifier, FeatureExtractor};
use crate::snapback::ContextTracker;
use crate::storage::Storage;
use crate::types::{
    CaptureEvent, EventType, FocusMode, PermissionStatus, PredictionRecord, SnapbackPayload,
};

pub struct AppState {
    pub storage: parking_lot::Mutex<Storage>,
    pub permissions: parking_lot::Mutex<PermissionStatus>,
    pub capture_running: parking_lot::Mutex<bool>,
    pub focus_mode: parking_lot::Mutex<FocusMode>,
    pub classifier: parking_lot::Mutex<Classifier>,
    pub latest_prediction: parking_lot::Mutex<Option<PredictionRecord>>,
    event_rx: parking_lot::Mutex<Option<std::sync::mpsc::Receiver<CaptureEvent>>>,
}

impl AppState {
    pub fn new(storage: Storage) -> Self {
        let permissions = crate::capture::check_permissions();
        let focus_mode = FocusMode::Normal;
        Self {
            storage: parking_lot::Mutex::new(storage),
            permissions: parking_lot::Mutex::new(permissions),
            capture_running: parking_lot::Mutex::new(false),
            focus_mode: parking_lot::Mutex::new(focus_mode),
            classifier: parking_lot::Mutex::new(Classifier::new(focus_mode)),
            latest_prediction: parking_lot::Mutex::new(None),
            event_rx: parking_lot::Mutex::new(None),
        }
    }

    pub fn start_engine(&self, app: AppHandle) -> Result<(), String> {
        let (tx, rx) = std::sync::mpsc::channel();
        crate::capture::start_capture_thread(tx);
        *self.event_rx.lock() = Some(rx);
        *self.capture_running.lock() = true;

        thread::spawn(move || run_engine_loop(app));
        Ok(())
    }
}

fn run_engine_loop(app: AppHandle) {
    let mut extractor = FeatureExtractor::new();
    let mut tracker = ContextTracker::new();
    let mut last_prediction_at = 0.0_f64;
    let mut deep_focus_started: Option<std::time::Instant> = None;
    let mut last_hyperfocus_alert_secs = 0_u64;

    loop {
        let Some(state) = app.try_state::<AppState>() else {
            break;
        };

        let events: Vec<CaptureEvent> = {
            let guard = state.event_rx.lock();
            if let Some(rx) = guard.as_ref() {
                let mut batch = Vec::new();
                while let Ok(event) = rx.try_recv() {
                    batch.push(event);
                }
                batch
            } else {
                Vec::new()
            }
        };

        for event in events {
            if matches!(
                event.event_type,
                EventType::WindowFocusChange | EventType::WindowTitleChange
            ) {
                tracker.on_window_change(&event.app_name, &event.window_title);
            } else {
                tracker.on_activity();
            }

            let features = extractor.update(&event);
            let now = features.timestamp;
            if now - last_prediction_at >= 1.0 {
                let focus_mode = *state.focus_mode.lock();
                state.classifier.lock().set_focus_mode(focus_mode);
                let scores = state.classifier.lock().predict(&features);
                extractor.update_focus_score(scores.focus_score / 100.0, 0.2);

                let session_id = state
                    .storage
                    .lock()
                    .get_active_session()
                    .ok()
                    .flatten()
                    .map(|s| s.session_id)
                    .unwrap_or_else(|| "idle".to_string());

                let record = PredictionRecord {
                    session_id: session_id.clone(),
                    focus_score: scores.focus_score,
                    distraction_risk: scores.distraction_risk,
                    focus_state: scores.focus_state.clone(),
                    timestamp: chrono::Utc::now().to_rfc3339(),
                };

                if let Err(err) = state.storage.lock().save_prediction(&record) {
                    log::warn!("failed to save prediction: {err}");
                }
                *state.latest_prediction.lock() = Some(record.clone());
                let _ = app.emit("prediction", &record);
                last_prediction_at = now;

                if scores.focus_state == "DEEP_FOCUS" {
                    if deep_focus_started.is_none() {
                        deep_focus_started = Some(std::time::Instant::now());
                    }
                } else {
                    deep_focus_started = None;
                }

                if let Some(started) = deep_focus_started {
                    let deep_secs = started.elapsed().as_secs();
                    if let Some(alert) =
                        check_hyperfocus(focus_mode, deep_secs, last_hyperfocus_alert_secs)
                    {
                        let _ = app.emit("hyperfocus", &alert);
                        last_hyperfocus_alert_secs = deep_secs;
                    }
                }
            }
        }

        if let Some(snapback) = tracker.take_pending_snapback() {
            let session_id = state
                .storage
                .lock()
                .get_active_session()
                .ok()
                .flatten()
                .map(|s| s.session_id)
                .unwrap_or_else(|| "idle".to_string());
            if let Err(err) = state.storage.lock().record_snapback(&session_id, &snapback.summary)
            {
                log::warn!("failed to record snapback: {err}");
            }

            let payload = SnapbackPayload {
                summary: snapback.summary.clone(),
                app_name: snapback.app_name,
                window_title: snapback.window_title,
                file_hint: snapback.file_hint,
                distraction_duration_secs: snapback.distraction_duration_secs,
            };
            show_snapback_overlay(&app, &payload);
            let _ = app.emit("snapback", &payload);
        }

        thread::sleep(std::time::Duration::from_millis(100));
    }
}

fn show_snapback_overlay(app: &AppHandle, payload: &SnapbackPayload) {
    if let Some(window) = app.get_webview_window("snapback") {
        let _ = window.show();
        let _ = window.set_focus();
        let _ = window.emit("snapback-data", payload);
        return;
    }

    let url = tauri::WebviewUrl::App("snapback.html".into());
    if let Ok(window) = tauri::WebviewWindowBuilder::new(app, "snapback", url)
        .title("Snapback")
        .inner_size(420.0, 220.0)
        .always_on_top(true)
        .decorations(true)
        .resizable(false)
        .build()
    {
        let _ = window.emit("snapback-data", payload);
    }
}
