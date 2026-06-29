#[derive(Debug, Clone, Default)]
pub struct ParsedTitle {
    pub file_hint: String,
    pub project_hint: String,
    pub summary: String,
}

pub fn parse_window_title(app_name: &str, window_title: &str) -> ParsedTitle {
    let title = window_title.trim();
    if title.is_empty() {
        return ParsedTitle {
            file_hint: String::new(),
            project_hint: String::new(),
            summary: format!("Working in {app_name}"),
        };
    }

    let lower_app = app_name.to_lowercase();
    let parts: Vec<&str> = title.split(" - ").collect();

    if lower_app.contains("code") || lower_app.contains("cursor") {
        let file = parts.first().unwrap_or(&title).trim();
        let project = parts.get(parts.len().saturating_sub(2)).unwrap_or(&"").trim();
        return ParsedTitle {
            file_hint: file.to_string(),
            project_hint: project.to_string(),
            summary: if project.is_empty() {
                format!("Editing {file}")
            } else {
                format!("Editing {file} in {project}")
            },
        };
    }

    if parts.len() >= 2 {
        let head = parts[0].trim();
        ParsedTitle {
            file_hint: head.to_string(),
            project_hint: parts[1].trim().to_string(),
            summary: format!("{head} — {}", parts[1].trim()),
        }
    } else {
        ParsedTitle {
            file_hint: title.to_string(),
            project_hint: String::new(),
            summary: format!("{title} ({app_name})"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_vscode_title() {
        let parsed = parse_window_title("Code", "auth.ts - snapback - Visual Studio Code");
        assert_eq!(parsed.file_hint, "auth.ts");
        assert!(parsed.summary.contains("auth.ts"));
    }
}
