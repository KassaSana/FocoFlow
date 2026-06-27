use crate::types::PermissionStatus;

pub fn check_permissions() -> PermissionStatus {
    let active_window_available = active_win_pos_rs::get_active_window().is_ok();
    let capture_available = probe_capture();

    let message = if !active_window_available && !capture_available {
        "Grant Accessibility and Input Monitoring permissions in System Settings, then restart FocoFlow.".to_string()
    } else if !active_window_available {
        "Active window detection unavailable. Grant Accessibility permission in System Settings.".to_string()
    } else if !capture_available {
        "Input capture unavailable. Grant Input Monitoring permission in System Settings.".to_string()
    } else {
        "Capture permissions look good.".to_string()
    };

    PermissionStatus {
        capture_available,
        active_window_available,
        message,
    }
}

#[cfg(target_os = "macos")]
fn probe_capture() -> bool {
    // rdev does not expose a permission API; we treat active window as a proxy
    // and rely on the capture thread logging failures at runtime.
    active_win_pos_rs::get_active_window().is_ok()
}

#[cfg(not(target_os = "macos"))]
fn probe_capture() -> bool {
    true
}
