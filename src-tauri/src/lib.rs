mod capture;
mod bench;
mod commands;
mod engine;
mod snapback;
mod state;
mod storage;
mod types;

use tauri::{Emitter, Manager};

use state::AppState;
use storage::Storage;

static PROCESS_START: std::sync::OnceLock<std::time::Instant> = std::sync::OnceLock::new();

pub fn run_from_cli(args: Vec<String>) -> i32 {
    let _ = PROCESS_START.set(std::time::Instant::now());

    if args.iter().any(|a| a == "--benchmark") {
        let bench_args = bench::parse_bench_args(&args);
        return bench::run_benchmark(bench_args);
    }

    run();
    0
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    env_logger::init();

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
            if let Some(t0) = PROCESS_START.get() {
                log::info!("startup_ms_to_setup={}", t0.elapsed().as_millis());
            }

            let app_data_dir = app
                .path()
                .app_data_dir()
                .expect("failed to resolve app data dir");
            let storage = Storage::open(app_data_dir).expect("failed to open storage");
            let app_state = AppState::new(storage);
            app.manage(app_state);

            let handle = app.handle().clone();
            if let Some(state) = handle.try_state::<AppState>() {
                let _ = state.start_engine(handle.clone());
            }

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::get_health,
            commands::get_latest_prediction,
            commands::get_prediction_history,
            commands::start_session,
            commands::stop_session,
            commands::get_session,
            commands::get_active_session,
            commands::submit_label,
            commands::get_session_recap,
            commands::set_focus_mode,
            commands::dismiss_snapback,
            commands::send_test_prediction,
            commands::refresh_permissions,
        ])
        .build(tauri::generate_context!())
        .expect("error while building Snapback");

    let handle = app.handle().clone();
    app.run(move |_app_handle, event| {
        if let tauri::RunEvent::Ready = event {
            if let Some(t0) = PROCESS_START.get() {
                log::info!("startup_ms_to_ready={}", t0.elapsed().as_millis());
            }
            let _ = handle.emit("snapback://ready", ());
        }
    });
}
