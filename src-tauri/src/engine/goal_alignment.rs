//! Match the active window against the user's session goal.
//!
//! Example: goal "fix the Rust classifier" → Cursor/terminal score high;
//! Slack scores lower unless the goal mentions communication.

use crate::engine::app_context::AppContext;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum GoalTheme {
    Coding,
    Writing,
    Design,
    Research,
    Communication,
    General,
}

#[derive(Debug, Clone)]
struct GoalProfile {
    themes: Vec<GoalTheme>,
}

const CODING_KEYWORDS: &[&str] = &[
    "code", "coding", "bug", "fix", "implement", "rust", "api", "refactor", "test", "debug",
    "build", "feature", "compile", "merge", "pr", "pull request",
];
const WRITING_KEYWORDS: &[&str] = &[
    "write", "writing", "report", "essay", "document", "blog", "draft", "paper", "notes",
];
const DESIGN_KEYWORDS: &[&str] = &["design", "figma", "ui", "ux", "mockup", "wireframe"];
const RESEARCH_KEYWORDS: &[&str] = &[
    "research", "read", "reading", "learn", "study", "article", "docs", "documentation",
];
const COMMUNICATION_KEYWORDS: &[&str] = &[
    "email", "meeting", "call", "slack", "message", "reply", "interview", "present",
];

fn goal_contains_any(goal: &str, keywords: &[&str]) -> bool {
    keywords.iter().any(|kw| goal.contains(kw))
}

fn parse_goal(goal: &str) -> GoalProfile {
    let g = goal.to_lowercase();
    let mut themes = Vec::new();

    if goal_contains_any(&g, CODING_KEYWORDS) {
        themes.push(GoalTheme::Coding);
    }
    if goal_contains_any(&g, WRITING_KEYWORDS) {
        themes.push(GoalTheme::Writing);
    }
    if goal_contains_any(&g, DESIGN_KEYWORDS) {
        themes.push(GoalTheme::Design);
    }
    if goal_contains_any(&g, RESEARCH_KEYWORDS) {
        themes.push(GoalTheme::Research);
    }
    if goal_contains_any(&g, COMMUNICATION_KEYWORDS) {
        themes.push(GoalTheme::Communication);
    }
    if themes.is_empty() {
        themes.push(GoalTheme::General);
    }

    GoalProfile { themes }
}

fn theme_alignment(theme: GoalTheme, ctx: &AppContext, title: &str) -> f64 {
    match theme {
        GoalTheme::Coding => {
            if ctx.is_ide || ctx.is_terminal {
                0.95
            } else if ctx.is_browser
                && (title.contains("github")
                    || title.contains("stackoverflow")
                    || title.contains("docs.rs")
                    || title.contains("documentation"))
            {
                0.8
            } else if ctx.is_communication {
                0.35
            } else if ctx.is_productivity {
                0.45
            } else {
                0.3
            }
        }
        GoalTheme::Writing => {
            if ctx.is_productivity {
                0.95
            } else if ctx.is_ide && (title.contains(".md") || title.contains("readme")) {
                0.85
            } else if ctx.is_browser && !ctx.title_is_distracting {
                0.55
            } else {
                0.35
            }
        }
        GoalTheme::Design => {
            if ctx.is_productivity && title.contains("figma") {
                0.95
            } else if title.contains("figma") {
                0.9
            } else if ctx.is_browser && !ctx.title_is_distracting {
                0.5
            } else {
                0.35
            }
        }
        GoalTheme::Research => {
            if ctx.is_browser && !ctx.title_is_distracting {
                0.85
            } else if ctx.is_productivity || ctx.is_ide {
                0.65
            } else {
                0.35
            }
        }
        GoalTheme::Communication => {
            if ctx.is_communication {
                0.9
            } else if ctx.is_browser && title.contains("mail") {
                0.85
            } else {
                0.35
            }
        }
        GoalTheme::General => 0.5,
    }
}

/// 0.0 = clearly misaligned, 0.5 = neutral/no goal, 1.0 = strongly aligned.
pub fn alignment_score(goal: &str, ctx: &AppContext, window_title: &str) -> f64 {
    let trimmed = goal.trim();
    if trimmed.is_empty() {
        return 0.5;
    }

    let profile = parse_goal(trimmed);
    let title = window_title.to_lowercase();

    profile
        .themes
        .iter()
        .map(|theme| theme_alignment(*theme, ctx, &title))
        .fold(0.0_f64, f64::max)
        .clamp(0.0, 1.0)
}

/// Convert alignment into a classifier bias centered at zero.
pub fn alignment_bias(goal: Option<&str>, ctx: &AppContext, window_title: &str) -> f64 {
    let score = goal
        .filter(|g| !g.trim().is_empty())
        .map(|g| alignment_score(g, ctx, window_title))
        .unwrap_or(0.5);
    score - 0.5
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::engine::app_context::classify;

    #[test]
    fn coding_goal_aligns_with_cursor() {
        let ctx = classify("Cursor", "classifier.rs — Snapback");
        let score = alignment_score("fix the rust classifier bug", &ctx, "classifier.rs");
        assert!(score >= 0.9, "score={score}");
    }

    #[test]
    fn coding_goal_misaligns_with_slack() {
        let ctx = classify("Slack", "#random");
        let score = alignment_score("implement the api endpoint", &ctx, "#random");
        assert!(score <= 0.4, "score={score}");
    }

    #[test]
    fn research_goal_aligns_with_docs_in_browser() {
        let ctx = classify("Google Chrome", "Rust documentation - std");
        let score = alignment_score("research tokio docs", &ctx, "Rust documentation");
        assert!(score >= 0.8, "score={score}");
    }

    #[test]
    fn empty_goal_is_neutral() {
        let ctx = classify("Cursor", "lib.rs");
        assert_eq!(alignment_score("", &ctx, "lib.rs"), 0.5);
        assert_eq!(alignment_bias(None, &ctx, "lib.rs"), 0.0);
    }
}
