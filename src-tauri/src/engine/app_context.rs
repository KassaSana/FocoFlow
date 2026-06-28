//! Single source of truth for “what kind of app is this?”
//!
//! Both the feature engine and snapback tracker read from here so they never
//! disagree about whether Slack is work and YouTube is a distraction.

use crate::types::{AppRuleKind, AppRuleRecord};

/// Flags describing the active window. `Copy` means small structs can be
/// passed by value cheaply (like copying a few bools on the stack).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct AppContext {
    pub is_browser: bool,
    pub is_ide: bool,
    pub is_communication: bool,
    pub is_entertainment: bool,
    pub is_productivity: bool,
    pub is_terminal: bool,
    /// True when the window *title* names a known distraction (e.g. “YouTube”).
    pub title_is_distracting: bool,
    /// User rule matched — treat as on-task even if defaults disagree.
    pub personal_allow: bool,
    /// User rule matched — treat as off-task even if defaults disagree.
    pub personal_block: bool,
}

impl AppContext {
    pub fn productivity_category(&self) -> &'static str {
        if self.personal_block {
            "Entertainment"
        } else if self.is_ide {
            "Building"
        } else if self.is_productivity || self.personal_allow {
            "Writing"
        } else if self.is_browser {
            "Browsing"
        } else if self.is_communication {
            "Communicating"
        } else if self.is_entertainment {
            "Entertainment"
        } else {
            "Unknown"
        }
    }
}

const BROWSERS: &[&str] = &["chrome", "msedge", "firefox", "brave", "opera", "safari"];
const IDES: &[&str] = &[
    "code", "cursor", "devenv", "idea", "pycharm", "clion", "rider", "xcode",
];
const COMMUNICATION: &[&str] = &["slack", "discord", "teams", "outlook", "zoom", "messages"];
const ENTERTAINMENT_APPS: &[&str] = &["spotify", "steam", "vlc", "netflix", "youtube"];
const PRODUCTIVITY: &[&str] = &[
    "word", "excel", "powerpnt", "notion", "obsidian", "pages", "figma",
];
const TERMINALS: &[&str] = &["terminal", "iterm", "warp", "alacritty", "kitty", "wezterm"];

/// Title keywords catch distractions inside browsers (e.g. “YouTube — Chrome”).
const DISTRACTING_TITLE_KEYWORDS: &[&str] = &[
    "youtube", "netflix", "twitter", "reddit", "instagram", "tiktok", "twitch",
    "facebook", "hulu", "disney+", "prime video",
];

fn matches_rule_pattern(pattern: &str, app_name: &str, window_title: &str) -> bool {
    app_name.contains(pattern) || window_title.contains(pattern)
}

fn apply_personal_rules(
    ctx: &mut AppContext,
    rules: &[AppRuleRecord],
    app_name: &str,
    window_title: &str,
) {
    let mut allow = false;
    let mut block = false;

    for rule in rules {
        if matches_rule_pattern(&rule.pattern, app_name, window_title) {
            match rule.rule_type {
                AppRuleKind::Allow => allow = true,
                AppRuleKind::Block => block = true,
            }
        }
    }

    // Block wins when both match — safer default for focus protection.
    if block {
        ctx.personal_block = true;
        ctx.personal_allow = false;
        ctx.is_entertainment = true;
        ctx.title_is_distracting = true;
    } else if allow {
        ctx.personal_allow = true;
        ctx.is_entertainment = false;
        ctx.title_is_distracting = false;
        ctx.is_productivity = true;
    }
}

fn base_classify(app_name: &str, window_title: &str) -> AppContext {
    let name = app_name.to_lowercase();
    let title = window_title.to_lowercase();

    let title_is_distracting = DISTRACTING_TITLE_KEYWORDS
        .iter()
        .any(|kw| title.contains(kw));

    AppContext {
        is_browser: BROWSERS.iter().any(|b| name.contains(b)),
        is_ide: IDES.iter().any(|b| name.contains(b)),
        is_communication: COMMUNICATION.iter().any(|b| name.contains(b)),
        is_entertainment: ENTERTAINMENT_APPS.iter().any(|b| name.contains(b)),
        is_productivity: PRODUCTIVITY.iter().any(|b| name.contains(b)),
        is_terminal: TERMINALS.iter().any(|b| name.contains(b)),
        title_is_distracting,
        personal_allow: false,
        personal_block: false,
    }
}

/// Classify from app name + window title, then layer personal allow/block rules.
pub fn classify(app_name: &str, window_title: &str, rules: &[AppRuleRecord]) -> AppContext {
    let name = app_name.to_lowercase();
    let title = window_title.to_lowercase();
    let mut ctx = base_classify(app_name, window_title);
    apply_personal_rules(&mut ctx, rules, &name, &title);
    ctx
}

/// Clearly not work — entertainment app or distracting site in the title.
pub fn is_clearly_off_task(ctx: &AppContext) -> bool {
    if ctx.personal_allow {
        return false;
    }
    ctx.personal_block || ctx.is_entertainment || ctx.title_is_distracting
}

/// Should snapback treat this moment as “on task” (worth saving / returning to)?
pub fn snapback_on_task(
    ctx: &AppContext,
    window_title: &str,
    focus_state: Option<&str>,
    session_goal: Option<&str>,
) -> bool {
    if is_clearly_off_task(ctx) {
        return false;
    }
    if focus_state == Some("DISTRACTED") {
        return false;
    }

    if ctx.personal_allow {
        return true;
    }

    if let Some(goal) = session_goal.filter(|g| !g.trim().is_empty()) {
        let alignment = crate::engine::goal_alignment::alignment_score(goal, ctx, window_title);
        if alignment >= 0.72 {
            return true;
        }
        if alignment <= 0.35 {
            return false;
        }
    }

    if ctx.is_ide || ctx.is_productivity || ctx.is_terminal {
        return true;
    }

    if ctx.is_communication {
        return true;
    }

    match focus_state {
        Some("PRODUCTIVE") | Some("DEEP_FOCUS") | Some("PSEUDO_PRODUCTIVE") => true,
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::types::AppRuleKind;

    fn allow_rule(pattern: &str) -> AppRuleRecord {
        AppRuleRecord {
            id: 1,
            pattern: pattern.to_string(),
            rule_type: AppRuleKind::Allow,
            note: None,
            created_at: String::new(),
            updated_at: String::new(),
        }
    }

    fn block_rule(pattern: &str) -> AppRuleRecord {
        AppRuleRecord {
            id: 2,
            pattern: pattern.to_string(),
            rule_type: AppRuleKind::Block,
            note: None,
            created_at: String::new(),
            updated_at: String::new(),
        }
    }

    #[test]
    fn cursor_is_ide_and_on_task() {
<<<<<<< HEAD
        let ctx = classify("Cursor", "classifier.rs — Snapback");
=======
        let ctx = classify("Cursor", "classifier.rs — FocoFlow", &[]);
>>>>>>> dd99d0b (applying personal app rules in classifier,snapback, engine cache)
        assert!(ctx.is_ide);
        assert!(snapback_on_task(&ctx, "classifier.rs — Snapback", None, None));
    }

    #[test]
    fn youtube_in_browser_title_is_off_task() {
        let ctx = classify("Google Chrome", "Rick Astley - YouTube", &[]);
        assert!(ctx.is_browser);
        assert!(ctx.title_is_distracting);
        assert!(!snapback_on_task(&ctx, "Rick Astley - YouTube", Some("PRODUCTIVE"), None));
    }

    #[test]
    fn personal_allow_makes_discord_on_task() {
        let rules = vec![allow_rule("discord")];
        let ctx = classify("Discord", "Study group", &rules);
        assert!(ctx.personal_allow);
        assert!(snapback_on_task(&ctx, "Study group", None, None));
    }

    #[test]
    fn personal_block_makes_notion_off_task() {
        let rules = vec![block_rule("notion")];
        let ctx = classify("Notion", "Weekly plan", &rules);
        assert!(ctx.personal_block);
        assert!(!snapback_on_task(&ctx, "Weekly plan", Some("PRODUCTIVE"), None));
    }

    #[test]
    fn block_wins_when_both_rules_match() {
        let rules = vec![allow_rule("slack"), block_rule("slack")];
        let ctx = classify("Slack", "#general", &rules);
        assert!(ctx.personal_block);
        assert!(!ctx.personal_allow);
    }

    #[test]
    fn classifier_distracted_overrides_slack() {
        let ctx = classify("Slack", "#general", &[]);
        assert!(ctx.is_communication);
        assert!(snapback_on_task(&ctx, "#general", Some("PRODUCTIVE"), None));
        assert!(!snapback_on_task(&ctx, "#general", Some("DISTRACTED"), None));
    }

    #[test]
    fn generic_browser_needs_classifier_or_stays_off_task() {
        let ctx = classify("Google Chrome", "New Tab", &[]);
        assert!(ctx.is_browser);
        assert!(!snapback_on_task(&ctx, "New Tab", None, None));
        assert!(snapback_on_task(&ctx, "New Tab", Some("PRODUCTIVE"), None));
    }

    #[test]
    fn research_goal_makes_docs_browser_on_task() {
        let ctx = classify("Google Chrome", "Rust documentation - std", &[]);
        assert!(snapback_on_task(
            &ctx,
            "Rust documentation",
            None,
            Some("research tokio docs"),
        ));
    }
}
