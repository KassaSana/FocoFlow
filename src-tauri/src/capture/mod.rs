pub mod active_window;
pub mod permissions;
pub mod thread;

pub use permissions::check_permissions;
pub use thread::start_capture_thread;
