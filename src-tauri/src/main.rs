#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let exit_code = snapback_lib::run_from_cli(args);
    std::process::exit(exit_code);
}
