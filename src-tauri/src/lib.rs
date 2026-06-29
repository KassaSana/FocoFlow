mod capture;
mod commands;
mod engine;
mod snapback;
mod state;
mod storage;
mod types;

use tauri::Manager;

use state::AppState;
use storage::Storage;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    env_logger::init();

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .setup(|app| {
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
        .run(tauri::generate_context!())
        .expect("error while running Snapback");
}
