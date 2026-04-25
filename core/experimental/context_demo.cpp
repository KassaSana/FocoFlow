/**
 * Neural Focus: Context Recovery Demo/Test
 * 
 * This file demonstrates how the context recovery system works.
 * Compile and run to see the overlay in action!
 * 
 * BUILD:
 *   cl /EHsc /std:c++17 context_demo.cpp /link user32.lib gdi32.lib
 * 
 * Or with g++ (MinGW):
 *   g++ -std=c++17 context_demo.cpp -o context_demo.exe -lgdi32 -luser32
 */

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <Windows.h>
#endif

// Include our context recovery components
#include "context.h"
#include "title_parser.h"
#include "overlay.h"
#include "context_tracker.h"

/**
 * TEST 1: Title Parser
 * 
 * Tests that we correctly extract context from various window titles.
 */
void test_title_parser() {
    printf("\n=== TEST 1: Title Parser ===\n\n");
    
    struct TestCase {
        const char* app_name;
        const char* window_title;
        const char* expected_file;
        int expected_line;
    };
    
    TestCase cases[] = {
        // VS Code titles
        {"Code.exe", "main.py - src - MyProject - Visual Studio Code", 
         "main.py", 0},
        {"Code.exe", "app.ts:45 - components - Frontend - Visual Studio Code",
         "app.ts", 45},
        {"Code.exe", "● Untitled-1 - Visual Studio Code",  // Unsaved file
         "Untitled-1", 0},
         
        // Chrome titles
        {"chrome.exe", "How to use mutexes in C++ - Stack Overflow - Google Chrome",
         "", 0},
        {"chrome.exe", "GitHub - user/repo - Pull Requests - Google Chrome",
         "", 0},
        {"chrome.exe", "YouTube - Google Chrome",  // Distracting!
         "", 0},
         
        // Terminal titles
        {"WindowsTerminal.exe", "MINGW64:/c/Users/dev/projects",
         "", 0},
        {"powershell.exe", "Administrator: Windows PowerShell",
         "", 0},
    };
    
    for (const auto& tc : cases) {
        ContextSnapshot ctx;
        parser::parse_window_title(tc.app_name, tc.window_title, ctx);
        
        printf("App: %s\n", tc.app_name);
        printf("Title: %s\n", tc.window_title);
        printf("  → File: \"%s\" (expected: \"%s\") %s\n", 
               ctx.file_path, 
               tc.expected_file,
               strcmp(ctx.file_path, tc.expected_file) == 0 ? "✓" : "✗");
        printf("  → Line: %d (expected: %d) %s\n",
               ctx.line_number,
               tc.expected_line,
               ctx.line_number == tc.expected_line ? "✓" : "✗");
        printf("  → Category: %d (%s)\n", 
               static_cast<int>(ctx.category),
               is_productive_category(ctx.category) ? "productive" : "distracting");
        printf("  → Brief: %s\n", ctx.brief_description());
        printf("\n");
    }
}

/**
 * TEST 2: Context History
 * 
 * Tests the circular buffer for storing context snapshots.
 */
void test_context_history() {
    printf("\n=== TEST 2: Context History ===\n\n");
    
    ContextHistory history;
    
    // Add some snapshots
    for (int i = 0; i < 5; i++) {
        ContextSnapshot snap;
        snap.clear();
        snap.timestamp_us = i * 30000000ULL;  // Every 30 seconds
        snap.duration_in_context_s = 30;
        snap.keystrokes_in_context = 100 + i * 50;
        snprintf(snap.file_path, sizeof(snap.file_path), "file%d.cpp", i);
        snap.category = AppCategory::IDE;
        snap.is_productive = true;
        
        history.push(snap);
        printf("Pushed snapshot %d: %s\n", i, snap.file_path);
    }
    
    printf("\nHistory size: %zu\n", history.size());
    
    // Test get_last
    const ContextSnapshot* last = history.get_last();
    printf("Last snapshot: %s ✓\n", last ? last->file_path : "(null)");
    
    // Test get_at
    printf("\nAccessing by index (0 = most recent):\n");
    for (size_t i = 0; i < history.size(); i++) {
        const ContextSnapshot* snap = history.get_at(i);
        printf("  [%zu] = %s\n", i, snap ? snap->file_path : "(null)");
    }
    
    // Test find_by_app
    ContextSnapshot ide_snap;
    ide_snap.clear();
    snprintf(ide_snap.app_name, sizeof(ide_snap.app_name), "Code.exe");
    snprintf(ide_snap.file_path, sizeof(ide_snap.file_path), "vscode_file.ts");
    ide_snap.duration_in_context_s = 60;
    ide_snap.keystrokes_in_context = 200;
    history.push(ide_snap);
    
    const ContextSnapshot* found = history.find_by_app("Code.exe");
    printf("\nFind by app 'Code.exe': %s %s\n", 
           found ? found->file_path : "(null)",
           found && strcmp(found->file_path, "vscode_file.ts") == 0 ? "✓" : "✗");
    
    // Test circular eviction
    printf("\nTesting circular eviction...\n");
    printf("Pushing %zu more snapshots (capacity is %zu)\n", 
           ContextHistory::CAPACITY, ContextHistory::CAPACITY);
    
    for (size_t i = 0; i < ContextHistory::CAPACITY; i++) {
        ContextSnapshot snap;
        snap.clear();
        snprintf(snap.file_path, sizeof(snap.file_path), "new_file%zu.cpp", i);
        snap.duration_in_context_s = 10;
        snap.keystrokes_in_context = 50;
        history.push(snap);
    }
    
    printf("After overflow, last file: %s\n", 
           history.get_last() ? history.get_last()->file_path : "(null)");
    printf("Size should still be %zu: %zu %s\n",
           ContextHistory::CAPACITY,
           history.size(),
           history.size() == ContextHistory::CAPACITY ? "✓" : "✗");
}

/**
 * TEST 3: State Machine
 * 
 * Tests the FOCUSED → DISTRACTED → RECOVERING transitions.
 */
void test_state_machine() {
    printf("\n=== TEST 3: State Machine ===\n\n");
    
    ContextTracker tracker;
    
    // Configure short distraction threshold for testing
    TrackerConfig config;
    config.min_distraction_for_recovery_ms = 1000;  // 1 second (for testing)
    tracker.set_config(config);
    
    tracker.start();
    printf("Initial state: %d (should be 0 = FOCUSED) %s\n",
           static_cast<int>(tracker.get_state()),
           tracker.get_state() == DistractionState::FOCUSED ? "✓" : "✗");
    
    // Simulate typing in VS Code
    printf("\nSimulating work in VS Code...\n");
    tracker.on_window_change("Code.exe", "main.py:100 - src - MyProject - Visual Studio Code");
    for (int i = 0; i < 100; i++) {
        tracker.on_keystroke();
    }
    tracker.update();
    printf("State after coding: %d (should be 0 = FOCUSED) %s\n",
           static_cast<int>(tracker.get_state()),
           tracker.get_state() == DistractionState::FOCUSED ? "✓" : "✗");
    
    // Simulate getting distracted by Twitter
    printf("\nSimulating switch to Twitter...\n");
    tracker.on_window_change("chrome.exe", "Home / Twitter - Google Chrome");
    printf("State after distraction: %d (should be 1 = DISTRACTED) %s\n",
           static_cast<int>(tracker.get_state()),
           tracker.get_state() == DistractionState::DISTRACTED ? "✓" : "✗");
    
    // Wait for distraction threshold
    printf("\nWaiting 1.5 seconds (> threshold)...\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    
    // Return to VS Code
    printf("Simulating return to VS Code...\n");
    tracker.on_window_change("Code.exe", "main.py:105 - src - MyProject - Visual Studio Code");
    printf("State after return: %d (should be 2 = RECOVERING) %s\n",
           static_cast<int>(tracker.get_state()),
           tracker.get_state() == DistractionState::RECOVERING ? "✓" : "✗");
    
    // In real usage, the overlay would be visible here
    // After dismissal, state returns to FOCUSED
}

/**
 * TEST 4: Recovery Overlay (Visual Test)
 * 
 * Shows the actual overlay window.
 * Only runs on Windows.
 */
void test_overlay_visual() {
    printf("\n=== TEST 4: Recovery Overlay (Visual) ===\n\n");
    
#ifdef _WIN32
    printf("Creating overlay window...\n");
    
    RecoveryOverlay overlay;
    OverlayConfig config;
    config.x = 100;
    config.y = 100;
    config.width = 450;
    config.height = 350;
    config.auto_dismiss_ms = 10000;  // 10 seconds
    
    if (!overlay.create(config)) {
        printf("ERROR: Failed to create overlay window!\n");
        return;
    }
    
    // Build sample recovery context
    RecoveryContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    
    // Last productive context
    strncpy(ctx.last_productive.file_path, "main.py", 
            sizeof(ctx.last_productive.file_path));
    ctx.last_productive.line_number = 234;
    strncpy(ctx.last_productive.project_name, "NeuralFocus",
            sizeof(ctx.last_productive.project_name));
    ctx.last_productive.keystrokes_in_context = 847;
    ctx.last_productive.duration_in_context_s = 45 * 60;  // 45 minutes
    ctx.last_productive.category = AppCategory::IDE;
    
    // Distraction info
    ctx.focus_duration_before_s = 45 * 60;
    ctx.distraction_duration_s = 180;  // 3 minutes
    strncpy(ctx.distraction_app, "Twitter", sizeof(ctx.distraction_app));
    
    // Recent activities
    ctx.activity_count = 3;
    strncpy(ctx.recent_activities[0].description, 
            "Edited ring_buffer.h (45 keystrokes)",
            sizeof(ctx.recent_activities[0].description));
    strncpy(ctx.recent_activities[1].description,
            "Browsed stackoverflow.com",
            sizeof(ctx.recent_activities[1].description));
    strncpy(ctx.recent_activities[2].description,
            "Edited main.py (234 keystrokes)",
            sizeof(ctx.recent_activities[2].description));
    
    printf("Showing overlay... (will auto-dismiss in 10 seconds)\n");
    printf("Press any key while overlay is focused to dismiss manually.\n\n");
    
    overlay.show(ctx);
    
    // Simple message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        
        // Exit when overlay is hidden
        if (!overlay.is_visible()) {
            break;
        }
    }
    
    overlay.destroy();
    printf("Overlay dismissed!\n");
#else
    printf("Visual test only available on Windows.\n");
#endif
}

/**
 * Main: Run all tests
 */
int main(int argc, char* argv[]) {
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║     Neural Focus: Context Recovery System Demo           ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    // Parse command line
    bool run_visual_test = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--visual") == 0 || strcmp(argv[i], "-v") == 0) {
            run_visual_test = true;
        }
    }
    
    // Run tests
    test_title_parser();
    test_context_history();
    test_state_machine();
    
    if (run_visual_test) {
        test_overlay_visual();
    } else {
        printf("\n----------------------------------------------------------\n");
        printf("Run with --visual flag to see the overlay window:\n");
        printf("  context_demo.exe --visual\n");
        printf("----------------------------------------------------------\n");
    }
    
    printf("\n=== All Tests Complete ===\n");
    return 0;
}
