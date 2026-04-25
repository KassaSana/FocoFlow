/**
 * Neural Focus: Window Title Parser
 * 
 * This file extracts meaningful context from window titles.
 * Different apps format their titles differently:
 * 
 *   VS Code:  "main.py - src - MyProject - Visual Studio Code"
 *   Chrome:   "GitHub - Pull Requests - Google Chrome"
 *   Terminal: "Administrator: Windows PowerShell"
 *   Word:     "Document1 - Microsoft Word"
 * 
 * By parsing these patterns, we can extract:
 *   - Current file being edited
 *   - Project/folder context
 *   - Domain being browsed
 *   - Whether there are unsaved changes
 * 
 * DESIGN PHILOSOPHY:
 * ==================
 * 
 * We don't need perfect accuracy - we need USEFUL context.
 * It's better to show "main.py in VS Code" even if we miss
 * the line number, than to show nothing at all.
 * 
 * PATTERN: Parser Combinator (simplified)
 * ========================================
 * 
 * Each app has its own parsing function that knows the title format.
 * We try them in order until one succeeds.
 * 
 * parseVSCode(title) → success/fail
 * parseChrome(title) → success/fail
 * parseTerminal(title) → success/fail
 * parseGeneric(title) → always succeeds (fallback)
 */

#ifndef NEUROFOCUS_TITLE_PARSER_H
#define NEUROFOCUS_TITLE_PARSER_H

#include "context.h"
#include <cstring>
#include <cstdio>
#include <cctype>

/**
 * EDUCATIONAL NOTE: String Parsing in C++
 * ========================================
 * 
 * C-style strings (char*) vs std::string:
 * 
 * C-style:
 *   const char* title = "main.py - VS Code";
 *   const char* dash = strstr(title, " - ");  // Find substring
 *   strncpy(dest, title, dash - title);       // Copy portion
 * 
 * std::string:
 *   std::string title = "main.py - VS Code";
 *   auto pos = title.find(" - ");
 *   auto part = title.substr(0, pos);
 * 
 * We use C-style because:
 * 1. ContextSnapshot uses char arrays (fixed size)
 * 2. No heap allocation (fast, predictable)
 * 3. Win32 APIs return char* anyway
 * 
 * But C-style string operations are error-prone!
 * Common bugs:
 * - Buffer overflow (strcpy without length check)
 * - Missing null terminator
 * - Off-by-one in length calculations
 * 
 * We use helper functions to make this safer.
 */

namespace parser {

//==============================================================================
// HELPER FUNCTIONS: Safe String Operations
//==============================================================================

/**
 * Safe string copy with guaranteed null termination
 * 
 * Unlike strcpy, this:
 * 1. Never writes beyond dest_size
 * 2. Always null-terminates
 * 3. Returns bytes written (useful for chaining)
 */
inline size_t safe_copy(char* dest, size_t dest_size, const char* src, size_t max_len = SIZE_MAX) {
    if (dest_size == 0) return 0;
    
    size_t to_copy = std::strlen(src);
    if (to_copy > max_len) to_copy = max_len;
    if (to_copy >= dest_size) to_copy = dest_size - 1;
    
    std::memcpy(dest, src, to_copy);
    dest[to_copy] = '\0';
    
    return to_copy;
}

/**
 * Safe copy from start to delimiter
 * 
 * Example: copy_until("main.py - VS Code", " - ", dest) → "main.py"
 */
inline bool copy_until(const char* src, const char* delimiter, char* dest, size_t dest_size) {
    const char* found = std::strstr(src, delimiter);
    if (!found) return false;
    
    size_t len = found - src;
    safe_copy(dest, dest_size, src, len);
    return true;
}

/**
 * Safe copy from after delimiter to end (or next delimiter)
 * 
 * Example: copy_after("main.py - VS Code", " - ", dest) → "VS Code"
 */
inline bool copy_after(const char* src, const char* delimiter, char* dest, size_t dest_size) {
    const char* found = std::strstr(src, delimiter);
    if (!found) return false;
    
    const char* start = found + std::strlen(delimiter);
    safe_copy(dest, dest_size, start);
    return true;
}

/**
 * Check if string ends with suffix
 */
inline bool ends_with(const char* str, const char* suffix) {
    size_t str_len = std::strlen(str);
    size_t suffix_len = std::strlen(suffix);
    
    if (suffix_len > str_len) return false;
    
    return std::strcmp(str + str_len - suffix_len, suffix) == 0;
}

/**
 * Check if string starts with prefix
 */
inline bool starts_with(const char* str, const char* prefix) {
    return std::strncmp(str, prefix, std::strlen(prefix)) == 0;
}

/**
 * Extract domain from URL
 * 
 * "https://stackoverflow.com/questions/123" → "stackoverflow.com"
 */
inline bool extract_domain(const char* url, char* domain, size_t domain_size) {
    // Skip protocol
    const char* start = url;
    if (starts_with(url, "https://")) start += 8;
    else if (starts_with(url, "http://")) start += 7;
    
    // Find end of domain (next /)
    const char* end = std::strchr(start, '/');
    size_t len = end ? (end - start) : std::strlen(start);
    
    safe_copy(domain, domain_size, start, len);
    return true;
}

/**
 * Strip leading/trailing whitespace
 */
inline void trim(char* str) {
    // Trim leading
    char* start = str;
    while (*start && std::isspace(*start)) start++;
    
    if (start != str) {
        std::memmove(str, start, std::strlen(start) + 1);
    }
    
    // Trim trailing
    char* end = str + std::strlen(str) - 1;
    while (end > str && std::isspace(*end)) {
        *end-- = '\0';
    }
}

//==============================================================================
// APP-SPECIFIC PARSERS
//==============================================================================

/**
 * Parse VS Code window title
 * 
 * Format: "filename - folder - project - Visual Studio Code"
 * With line: "filename:123 - folder - project - Visual Studio Code"
 * Unsaved:   "● filename - folder - project - Visual Studio Code"
 * 
 * Returns: true if this looks like a VS Code title
 */
inline bool parse_vscode(const char* title, ContextSnapshot& out) {
    // Must end with VS Code identifier
    if (!ends_with(title, "Visual Studio Code") && 
        !ends_with(title, "VS Code") &&
        !ends_with(title, "Code")) {
        return false;
    }
    
    out.category = AppCategory::IDE;
    
    // Check for unsaved indicator
    out.has_unsaved_changes = starts_with(title, "●") || starts_with(title, "• ");
    
    // Skip unsaved indicator if present
    const char* start = title;
    if (out.has_unsaved_changes) {
        start += (title[0] == '●') ? 2 : 3;  // Skip "● " or "• "
    }
    
    // Extract filename (before first " - ")
    char filename[128] = {0};
    if (!copy_until(start, " - ", filename, sizeof(filename))) {
        // No delimiter, might be welcome page
        safe_copy(out.window_title, sizeof(out.window_title), title);
        return true;
    }
    
    // Check for line number (filename:123)
    char* colon = std::strchr(filename, ':');
    if (colon && std::isdigit(*(colon + 1))) {
        out.line_number = std::atoi(colon + 1);
        *colon = '\0';  // Remove line number from filename
    }
    
    // Store filename as file_path (just the name, not full path)
    safe_copy(out.file_path, sizeof(out.file_path), filename);
    trim(out.file_path);
    
    // Try to extract project name (last part before VS Code)
    // "file - folder - project - VS Code"
    const char* parts[10];
    int part_count = 0;
    const char* current = start;
    
    while (part_count < 10) {
        parts[part_count++] = current;
        const char* next = std::strstr(current, " - ");
        if (!next) break;
        current = next + 3;
    }
    
    // Project name is usually second-to-last part
    if (part_count >= 3) {
        safe_copy(out.project_name, sizeof(out.project_name), parts[part_count - 2]);
        // Find the end of project name
        char* end = std::strstr(out.project_name, " - ");
        if (end) *end = '\0';
        trim(out.project_name);
    }
    
    // Check for debugging
    out.is_debugging = std::strstr(title, "[Debug]") != nullptr ||
                       std::strstr(title, "Debugging") != nullptr;
    
    return true;
}

/**
 * Parse Chrome/Edge/Firefox window title
 * 
 * Format: "Page Title - Site Name - Browser Name"
 * Example: "How to parse strings in C++ - Stack Overflow - Google Chrome"
 *          "GitHub - MyProject - Pull Requests - Google Chrome"
 * 
 * Returns: true if this looks like a browser title
 */
inline bool parse_browser(const char* title, ContextSnapshot& out) {
    // Check for browser identifiers
    bool is_chrome = ends_with(title, "Google Chrome") || 
                     ends_with(title, "Chrome");
    bool is_edge = ends_with(title, "Microsoft Edge") || 
                   ends_with(title, "Edge");
    bool is_firefox = ends_with(title, "Mozilla Firefox") || 
                      ends_with(title, "Firefox");
    
    if (!is_chrome && !is_edge && !is_firefox) {
        return false;
    }
    
    // Determine category based on known sites
    const char* productive_domains[] = {
        "GitHub", "Stack Overflow", "stackoverflow", 
        "MDN", "docs.", "documentation", "Wikipedia",
        "Microsoft Learn", "Google Docs", "Notion",
        "localhost", "127.0.0.1"
    };
    
    const char* distracting_domains[] = {
        "YouTube", "Twitter", "Facebook", "Instagram",
        "Reddit", "Netflix", "Twitch", "TikTok",
        "Discord", "Amazon", "eBay"
    };
    
    // Default to browser (neutral)
    out.category = AppCategory::BROWSER;
    
    // Check for productive sites
    for (const char* domain : productive_domains) {
        if (std::strstr(title, domain)) {
            out.category = AppCategory::DOCUMENTATION;
            // Extract domain
            safe_copy(out.browser_domain, sizeof(out.browser_domain), domain);
            break;
        }
    }
    
    // Check for distracting sites
    for (const char* domain : distracting_domains) {
        if (std::strstr(title, domain)) {
            if (std::strcmp(domain, "YouTube") == 0) {
                out.category = AppCategory::ENTERTAINMENT;
            } else if (std::strcmp(domain, "Amazon") == 0 || 
                       std::strcmp(domain, "eBay") == 0) {
                out.category = AppCategory::SHOPPING;
            } else if (std::strcmp(domain, "Discord") == 0) {
                out.category = AppCategory::COMMUNICATION;
            } else {
                out.category = AppCategory::SOCIAL_MEDIA;
            }
            safe_copy(out.browser_domain, sizeof(out.browser_domain), domain);
            break;
        }
    }
    
    // Check for search query (common format: "query - Google Search")
    if (std::strstr(title, "- Google Search") || 
        std::strstr(title, "- Bing") ||
        std::strstr(title, "- DuckDuckGo")) {
        // Extract the search query
        copy_until(title, " - ", out.last_search_query, sizeof(out.last_search_query));
        out.category = AppCategory::DOCUMENTATION;  // Searching is productive
    }
    
    return true;
}

/**
 * Parse terminal/console window title
 * 
 * Format varies by terminal:
 *   Windows Terminal: "PowerShell" or "Command Prompt" or custom
 *   cmd.exe: "C:\Windows\System32\cmd.exe"
 *   PowerShell: "Administrator: Windows PowerShell"
 *   Git Bash: "MINGW64:/c/Users/..."
 */
inline bool parse_terminal(const char* title, ContextSnapshot& out) {
    // Terminal identifiers
    bool is_terminal = 
        std::strstr(title, "PowerShell") != nullptr ||
        std::strstr(title, "cmd.exe") != nullptr ||
        std::strstr(title, "Command Prompt") != nullptr ||
        std::strstr(title, "Windows Terminal") != nullptr ||
        std::strstr(title, "MINGW") != nullptr ||
        std::strstr(title, "Git Bash") != nullptr ||
        std::strstr(title, "Bash") != nullptr ||
        std::strstr(title, "Terminal") != nullptr;
    
    if (!is_terminal) {
        return false;
    }
    
    out.category = AppCategory::TERMINAL;
    out.is_productive = true;
    
    // Try to extract current directory (if in title)
    // Many terminals show the current path
    if (std::strstr(title, "MINGW64:") || std::strstr(title, "MINGW32:")) {
        // Git Bash format: "MINGW64:/c/Users/project"
        const char* path_start = std::strchr(title, ':');
        if (path_start) {
            // Convert /c/Users to C:\Users
            safe_copy(out.file_path, sizeof(out.file_path), path_start + 1);
        }
    }
    
    return true;
}

/**
 * Parse JetBrains IDE titles (IntelliJ, PyCharm, WebStorm, etc.)
 * 
 * Format: "project – filename – IDE Name"
 * Note: JetBrains uses en-dash (–) not hyphen (-)
 */
inline bool parse_jetbrains(const char* title, ContextSnapshot& out) {
    // JetBrains identifiers
    bool is_jetbrains = 
        std::strstr(title, "IntelliJ IDEA") != nullptr ||
        std::strstr(title, "PyCharm") != nullptr ||
        std::strstr(title, "WebStorm") != nullptr ||
        std::strstr(title, "CLion") != nullptr ||
        std::strstr(title, "Rider") != nullptr ||
        std::strstr(title, "GoLand") != nullptr ||
        std::strstr(title, "RubyMine") != nullptr;
    
    if (!is_jetbrains) {
        return false;
    }
    
    out.category = AppCategory::IDE;
    out.is_productive = true;
    
    // JetBrains format: "project – filename – IDE"
    // Note: Uses en-dash (–, U+2013), not hyphen (-)
    const char* dash = std::strstr(title, " – ");  // en-dash
    if (!dash) dash = std::strstr(title, " - ");   // Fall back to hyphen
    
    if (dash) {
        // First part is project name
        safe_copy(out.project_name, sizeof(out.project_name), title, dash - title);
        trim(out.project_name);
        
        // Second part is filename
        const char* second_start = dash + 3;
        const char* second_end = std::strstr(second_start, " – ");
        if (!second_end) second_end = std::strstr(second_start, " - ");
        
        if (second_end) {
            safe_copy(out.file_path, sizeof(out.file_path), second_start, second_end - second_start);
            trim(out.file_path);
        }
    }
    
    return true;
}

/**
 * Parse Microsoft Office titles (Word, Excel, etc.)
 * 
 * Format: "Document Name - Microsoft Application"
 */
inline bool parse_office(const char* title, ContextSnapshot& out) {
    bool is_office = 
        ends_with(title, "Word") ||
        ends_with(title, "Excel") ||
        ends_with(title, "PowerPoint") ||
        ends_with(title, "Outlook") ||
        ends_with(title, "OneNote") ||
        std::strstr(title, "Microsoft Word") != nullptr ||
        std::strstr(title, "Microsoft Excel") != nullptr;
    
    if (!is_office) {
        return false;
    }
    
    out.category = AppCategory::PRODUCTIVITY;
    out.is_productive = true;
    
    // Extract document name (before " - ")
    copy_until(title, " - ", out.file_path, sizeof(out.file_path));
    trim(out.file_path);
    
    // Check for unsaved indicator
    out.has_unsaved_changes = std::strchr(out.file_path, '*') != nullptr;
    
    return true;
}

//==============================================================================
// MAIN PARSING FUNCTION
//==============================================================================

/**
 * Parse any window title into ContextSnapshot
 * 
 * Tries each app-specific parser in order.
 * Falls back to generic parsing if none match.
 * 
 * PATTERN: Chain of Responsibility
 * ================================
 * 
 * Each parser can:
 * 1. Handle the request (return true)
 * 2. Pass to next parser (return false)
 * 
 * Order matters! More specific parsers go first.
 */
inline bool parse_window_title(const char* app_name, const char* title, ContextSnapshot& out) {
    // Initialize output
    std::memset(&out, 0, sizeof(ContextSnapshot));
    
    // Copy raw data
    safe_copy(out.app_name, sizeof(out.app_name), app_name);
    safe_copy(out.window_title, sizeof(out.window_title), title);
    
    // Try each parser
    if (parse_vscode(title, out)) return true;
    if (parse_jetbrains(title, out)) return true;
    if (parse_office(title, out)) return true;
    if (parse_terminal(title, out)) return true;
    if (parse_browser(title, out)) return true;
    
    // Generic fallback: just keep the title, classify as unknown
    out.category = AppCategory::UNKNOWN;
    return true;  // Still succeeded (have raw title)
}

} // namespace parser

#endif // NEUROFOCUS_TITLE_PARSER_H
