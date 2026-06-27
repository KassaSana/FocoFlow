use std::sync::{Arc, RwLock};
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use rdev::{Event, EventType, Key};

use crate::capture::active_window::get_active_window_info;
use crate::types::{CaptureEvent, EventType as AppEventType};

pub fn start_capture_thread(
    event_tx: std::sync::mpsc::Sender<CaptureEvent>,
) -> thread::JoinHandle<()> {
    let last_window: Arc<RwLock<Option<(String, String)>>> = Arc::new(RwLock::new(None));
    let last_mouse_sample: Arc<RwLock<SystemTime>> =
        Arc::new(RwLock::new(SystemTime::now()));

    let poll_window = Arc::clone(&last_window);
    let poll_tx = event_tx.clone();
    thread::spawn(move || loop {
        if let Some(info) = get_active_window_info() {
            let mut changed = false;
            let mut title_only = false;
            {
                let guard = poll_window.read().ok();
                if let Some(current) = guard.as_ref().and_then(|g| g.as_ref()) {
                    changed = current.0 != info.app_name || current.1 != info.window_title;
                    title_only = current.0 == info.app_name && current.1 != info.window_title;
                } else {
                    changed = true;
                }
            }

            if changed {
                let now = timestamp_secs();
                let _ = poll_tx.send(CaptureEvent {
                    event_type: if title_only {
                        AppEventType::WindowTitleChange
                    } else {
                        AppEventType::WindowFocusChange
                    },
                    timestamp_secs: now,
                    app_name: info.app_name.clone(),
                    window_title: info.window_title.clone(),
                    mouse_x: 0,
                    mouse_y: 0,
                    mouse_speed: 0,
                    idle_duration_ms: 0,
                });
                if let Ok(mut guard) = poll_window.write() {
                    *guard = Some((info.app_name, info.window_title));
                }
            }
        }
        thread::sleep(Duration::from_millis(500));
    });

    thread::spawn(move || {
        let callback = move |event: Event| {
            let now = timestamp_secs();
            match event.event_type {
                EventType::KeyPress(key) => {
                    if matches!(key, Key::Unknown(_)) {
                        return;
                    }
                    let (app, title) = read_window(&last_window);
                    let _ = event_tx.send(CaptureEvent {
                        event_type: AppEventType::KeyPress,
                        timestamp_secs: now,
                        app_name: app,
                        window_title: title,
                        mouse_x: 0,
                        mouse_y: 0,
                        mouse_speed: 0,
                        idle_duration_ms: 0,
                    });
                }
                EventType::ButtonPress(_) => {
                    let (app, title) = read_window(&last_window);
                    let _ = event_tx.send(CaptureEvent {
                        event_type: AppEventType::MouseClick,
                        timestamp_secs: now,
                        app_name: app,
                        window_title: title,
                        mouse_x: 0,
                        mouse_y: 0,
                        mouse_speed: 0,
                        idle_duration_ms: 0,
                    });
                }
                EventType::MouseMove { x, y } => {
                    let should_sample = last_mouse_sample
                        .read()
                        .ok()
                        .and_then(|t| t.elapsed().ok())
                        .map(|d| d >= Duration::from_millis(50))
                        .unwrap_or(true);
                    if !should_sample {
                        return;
                    }
                    if let Ok(mut sample) = last_mouse_sample.write() {
                        *sample = SystemTime::now();
                    }
                    let (app, title) = read_window(&last_window);
                    let _ = event_tx.send(CaptureEvent {
                        event_type: AppEventType::MouseMove,
                        timestamp_secs: now,
                        app_name: app,
                        window_title: title,
                        mouse_x: x as i32,
                        mouse_y: y as i32,
                        mouse_speed: 0,
                        idle_duration_ms: 0,
                    });
                }
                _ => {}
            }
        };

        if let Err(err) = rdev::listen(callback) {
            log::error!("capture thread stopped: {err:?}");
        }
    })
}

fn read_window(last_window: &Arc<RwLock<Option<(String, String)>>>) -> (String, String) {
    last_window
        .read()
        .ok()
        .and_then(|g| g.clone())
        .unwrap_or_else(|| ("Unknown".to_string(), String::new()))
}

fn timestamp_secs() -> f64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}
