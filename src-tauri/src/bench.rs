use std::time::{Duration, Instant};

use sysinfo::{CpuRefreshKind, MemoryRefreshKind, ProcessRefreshKind, RefreshKind, System};

use crate::engine::classifier::Classifier;
use crate::engine::features::FeatureVector;
use crate::types::FocusMode;

#[derive(Debug, Clone)]
pub struct BenchArgs {
    pub runs: usize,
    pub warmup: usize,
    pub soak_seconds: u64,
    pub goal: Option<String>,
}

impl Default for BenchArgs {
    fn default() -> Self {
        Self {
            runs: 10_000,
            warmup: 1_000,
            soak_seconds: 0,
            goal: None,
        }
    }
}

fn parse_usize_flag(args: &[String], flag: &str) -> Option<usize> {
    args.iter()
        .position(|a| a == flag)
        .and_then(|idx| args.get(idx + 1))
        .and_then(|v| v.parse::<usize>().ok())
}

fn parse_u64_flag(args: &[String], flag: &str) -> Option<u64> {
    args.iter()
        .position(|a| a == flag)
        .and_then(|idx| args.get(idx + 1))
        .and_then(|v| v.parse::<u64>().ok())
}

fn parse_string_flag(args: &[String], flag: &str) -> Option<String> {
    args.iter()
        .position(|a| a == flag)
        .and_then(|idx| args.get(idx + 1))
        .map(|v| v.to_string())
}

pub fn parse_bench_args(args: &[String]) -> BenchArgs {
    let mut out = BenchArgs::default();

    if let Some(runs) = parse_usize_flag(args, "--runs") {
        out.runs = runs.max(1);
    }
    if let Some(warmup) = parse_usize_flag(args, "--warmup") {
        out.warmup = warmup;
    }
    if let Some(soak) = parse_u64_flag(args, "--soak-seconds") {
        out.soak_seconds = soak;
    }
    out.goal = parse_string_flag(args, "--goal");

    out
}

fn stable_features() -> FeatureVector {
    FeatureVector {
        app_name: "Cursor".to_string(),
        window_title: "classifier.rs — Snapback".to_string(),
        context_switches_30s: 0,
        context_switches_5min: 1,
        unique_apps_5min: 1,
        idle_time_30s: 0.0,
        keystroke_rate: 3.0,
        keystroke_count: 8,
        keystroke_interval_std: 0.15,
        time_in_current_app: 200,
        window_title_changed_30s: false,
        is_entertainment: false,
        is_communication: false,
        is_ide: true,
        is_productivity: false,
        ..FeatureVector::empty(0.0)
    }
}

fn pctl(sorted_micros: &[u128], p: f64) -> u128 {
    if sorted_micros.is_empty() {
        return 0;
    }
    let p = p.clamp(0.0, 100.0);
    let idx = ((p / 100.0) * ((sorted_micros.len() - 1) as f64)).round() as usize;
    sorted_micros[idx]
}

fn refresh_system() -> System {
    let refresh = RefreshKind::new()
        .with_memory(MemoryRefreshKind::everything())
        .with_cpu(CpuRefreshKind::everything())
        .with_processes(ProcessRefreshKind::everything());

    let mut sys = System::new_with_specifics(refresh);
    sys.refresh_all();
    sys
}

fn refresh_current_process(sys: &mut System) {
    let Ok(pid) = sysinfo::get_current_pid() else {
        return;
    };
    sys.refresh_process(pid);
}

fn current_process_kib(sys: &System) -> Option<u64> {
    let pid = sysinfo::get_current_pid().ok()?;
    let p = sys.process(pid)?;
    Some(p.memory())
}

fn current_process_cpu_percent(sys: &System) -> Option<f32> {
    let pid = sysinfo::get_current_pid().ok()?;
    let p = sys.process(pid)?;
    Some(p.cpu_usage())
}

pub fn run_benchmark(args: BenchArgs) -> i32 {
    let bench_start = Instant::now();

    let mut sys = refresh_system();
    let mem0_kib = current_process_kib(&sys).unwrap_or(0);

    let classifier = Classifier::new(FocusMode::Normal);
    let features = stable_features();
    let goal = args.goal.as_deref();

    // Warmup
    for _ in 0..args.warmup {
        let _ = classifier.predict(&features, goal);
    }

    // Timed runs
    let mut times: Vec<u128> = Vec::with_capacity(args.runs);
    for _ in 0..args.runs {
        let t0 = Instant::now();
        let _ = classifier.predict(&features, goal);
        times.push(t0.elapsed().as_micros());
    }
    times.sort_unstable();

    // CPU usage needs two refreshes spaced apart.
    refresh_current_process(&mut sys);
    std::thread::sleep(Duration::from_millis(200));
    refresh_current_process(&mut sys);

    let mem1_kib = current_process_kib(&sys).unwrap_or(0);
    let cpu_pct = current_process_cpu_percent(&sys).unwrap_or(0.0);

    let p50 = pctl(&times, 50.0);
    let p95 = pctl(&times, 95.0);
    let p99 = pctl(&times, 99.0);

    println!("SNAPBACK_BENCH v1");
    println!("mode=inference");
    println!("runs={}", args.runs);
    println!("warmup={}", args.warmup);
    println!("goal_present={}", goal.is_some());
    println!("latency_us_p50={}", p50);
    println!("latency_us_p95={}", p95);
    println!("latency_us_p99={}", p99);
    println!("mem_kib_before={}", mem0_kib);
    println!("mem_kib_after={}", mem1_kib);
    println!("cpu_pct_sample={:.2}", cpu_pct);
    println!("bench_elapsed_ms={}", bench_start.elapsed().as_millis());

    if args.soak_seconds > 0 {
        let soak_start = Instant::now();
        let mut iters: u64 = 0;
        let mut last_report = Instant::now();

        while soak_start.elapsed().as_secs() < args.soak_seconds {
            let _ = classifier.predict(&features, goal);
            iters += 1;

            if last_report.elapsed() >= Duration::from_secs(5) {
                refresh_current_process(&mut sys);
                let mem_kib = current_process_kib(&sys).unwrap_or(0);
                let cpu = current_process_cpu_percent(&sys).unwrap_or(0.0);
                println!(
                    "soak_elapsed_s={} iters={} mem_kib={} cpu_pct={:.2}",
                    soak_start.elapsed().as_secs(),
                    iters,
                    mem_kib,
                    cpu
                );
                last_report = Instant::now();
            }
        }

        println!("mode=soak_done");
        println!("soak_seconds={}", args.soak_seconds);
        println!("soak_iters={}", iters);
    }

    0
}

