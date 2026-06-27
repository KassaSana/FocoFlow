use active_win_pos_rs::ActiveWindow;

pub struct ActiveWindowInfo {
    pub app_name: String,
    pub window_title: String,
}

pub fn get_active_window_info() -> Option<ActiveWindowInfo> {
    match active_win_pos_rs::get_active_window() {
        Ok(ActiveWindow {
            app_name,
            title,
            ..
        }) => Some(ActiveWindowInfo {
            app_name,
            window_title: title,
        }),
        Err(_) => None,
    }
}
