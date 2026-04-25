/**
 * Neural Focus: Recovery Overlay Window
 * 
 * This file implements the floating overlay that appears when you return
 * from a distraction, showing you what you were doing before.
 * 
 * VISUAL DESIGN:
 * ==============
 * 
 *  ┌─────────────────────────────────────────┐
 *  │  🧠 Welcome back! You were:             │
 *  │                                         │
 *  │  📄 main.py:234                         │
 *  │  📁 Project: NeuralFocus                │
 *  │  ⏱️  Focused for: 45 minutes            │
 *  │                                         │
 *  │  Recent activity:                       │
 *  │    • Edited ring_buffer.h (23 min ago)  │
 *  │    • Browsed stackoverflow.com          │
 *  │                                         │
 *  │  [Press any key to dismiss]             │
 *  └─────────────────────────────────────────┘
 * 
 * TECHNICAL IMPLEMENTATION:
 * =========================
 * 
 * We use a Win32 layered window that:
 * 1. Stays on top of all windows (WS_EX_TOPMOST)
 * 2. Has transparency (WS_EX_LAYERED)
 * 3. Doesn't steal focus (WS_EX_NOACTIVATE)
 * 4. Auto-dismisses after 5 seconds
 * 5. Dismisses on any keyboard input
 * 
 * EDUCATIONAL NOTE: Win32 Window Styles
 * =====================================
 * 
 * Windows are created with style flags:
 * 
 * WS_POPUP:        No title bar, borders, or frame
 * WS_EX_TOPMOST:   Always on top of other windows
 * WS_EX_LAYERED:   Supports transparency (alpha blending)
 * WS_EX_TOOLWINDOW: Doesn't show in taskbar
 * WS_EX_NOACTIVATE: Clicking doesn't steal focus
 * 
 * We combine these for a floating notification overlay.
 */

#ifndef NEUROFOCUS_OVERLAY_H
#define NEUROFOCUS_OVERLAY_H

#ifdef _WIN32

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <functional>

#include "context.h"

/**
 * OverlayConfig: Customization options for the overlay
 */
struct OverlayConfig {
    // Position (screen coordinates)
    int x = 100;             // Distance from left
    int y = 100;             // Distance from top
    
    // Size
    int width = 400;         // Window width
    int height = 300;        // Window height
    
    // Timing
    int auto_dismiss_ms = 5000;  // Auto-hide after 5 seconds (0 = never)
    
    // Appearance
    uint8_t opacity = 230;       // 0-255 (0 = invisible, 255 = opaque)
    COLORREF background = RGB(30, 30, 40);    // Dark background
    COLORREF text_color = RGB(220, 220, 220); // Light text
    COLORREF accent = RGB(100, 200, 100);     // Green accent
    
    // Font
    int font_size = 16;
    const char* font_name = "Segoe UI";
};

/**
 * RecoveryOverlay: The floating context recovery window
 * 
 * Lifecycle:
 * 1. Create() - Registers window class, creates window (hidden)
 * 2. Show(context) - Populates data and shows window
 * 3. Hide() - Hides window (can show again later)
 * 4. Destroy() - Cleanup, must call before program exit
 * 
 * PATTERN: RAII (Resource Acquisition Is Initialization)
 * ======================================================
 * 
 * The constructor acquires resources (window handle).
 * The destructor releases resources (destroys window).
 * This ensures cleanup even if exceptions occur.
 * 
 * RecoveryOverlay overlay;  // Window created
 * overlay.show(ctx);        // Shows window
 * // ... when overlay goes out of scope, window destroyed
 */
class RecoveryOverlay {
public:
    //==========================================================================
    // LIFECYCLE
    //==========================================================================
    
    RecoveryOverlay() = default;
    ~RecoveryOverlay() { destroy(); }
    
    // Non-copyable (window handles can't be copied)
    RecoveryOverlay(const RecoveryOverlay&) = delete;
    RecoveryOverlay& operator=(const RecoveryOverlay&) = delete;
    
    /**
     * Initialize the overlay window
     * 
     * Must be called before show().
     * Returns: true if successful
     */
    bool create(const OverlayConfig& config = {}) {
        if (hwnd_) return true;  // Already created
        
        config_ = config;
        
        // Register window class (once per process)
        static bool class_registered = false;
        if (!class_registered) {
            WNDCLASSEXA wc = {0};
            wc.cbSize = sizeof(wc);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WindowProc;
            wc.hInstance = GetModuleHandleA(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;  // We paint our own background
            wc.lpszClassName = "NeuralFocusOverlay";
            
            if (!RegisterClassExA(&wc)) {
                return false;
            }
            class_registered = true;
        }
        
        // Create extended style flags
        DWORD ex_style = 
            WS_EX_TOPMOST |      // Always on top
            WS_EX_LAYERED |      // Support transparency
            WS_EX_TOOLWINDOW |   // No taskbar button
            WS_EX_NOACTIVATE;    // Don't steal focus
        
        // Create the window (initially hidden)
        hwnd_ = CreateWindowExA(
            ex_style,
            "NeuralFocusOverlay",
            "Neural Focus - Context Recovery",
            WS_POPUP,            // No borders/title
            config_.x, config_.y,
            config_.width, config_.height,
            nullptr,             // No parent
            nullptr,             // No menu
            GetModuleHandleA(nullptr),
            this                 // Pass 'this' for WndProc
        );
        
        if (!hwnd_) {
            return false;
        }
        
        // Set window transparency
        SetLayeredWindowAttributes(hwnd_, 0, config_.opacity, LWA_ALPHA);
        
        // Create fonts
        font_title_ = CreateFontA(
            config_.font_size + 4, 0, 0, 0,
            FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, config_.font_name
        );
        
        font_body_ = CreateFontA(
            config_.font_size, 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, config_.font_name
        );
        
        return true;
    }
    
    /**
     * Show the overlay with recovery context
     */
    void show(const RecoveryContext& context) {
        if (!hwnd_) return;
        
        context_ = context;
        
        // Show and invalidate (trigger repaint)
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        InvalidateRect(hwnd_, nullptr, TRUE);
        
        // Set auto-dismiss timer
        if (config_.auto_dismiss_ms > 0) {
            SetTimer(hwnd_, TIMER_AUTO_DISMISS, config_.auto_dismiss_ms, nullptr);
        }
    }
    
    /**
     * Hide the overlay
     */
    void hide() {
        if (!hwnd_) return;
        
        KillTimer(hwnd_, TIMER_AUTO_DISMISS);
        ShowWindow(hwnd_, SW_HIDE);
    }
    
    /**
     * Check if overlay is currently visible
     */
    bool is_visible() const {
        return hwnd_ && IsWindowVisible(hwnd_);
    }
    
    /**
     * Cleanup
     */
    void destroy() {
        if (font_title_) {
            DeleteObject(font_title_);
            font_title_ = nullptr;
        }
        if (font_body_) {
            DeleteObject(font_body_);
            font_body_ = nullptr;
        }
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }
    
    /**
     * Set callback for when user dismisses
     */
    void on_dismiss(std::function<void()> callback) {
        on_dismiss_ = callback;
    }
    
private:
    //==========================================================================
    // WIN32 INTERNALS
    //==========================================================================
    
    HWND hwnd_ = nullptr;
    HFONT font_title_ = nullptr;
    HFONT font_body_ = nullptr;
    OverlayConfig config_;
    RecoveryContext context_;
    std::function<void()> on_dismiss_;
    
    static constexpr UINT_PTR TIMER_AUTO_DISMISS = 1;
    
    /**
     * Window Procedure (message handler)
     * 
     * EDUCATIONAL NOTE: Win32 Message Loop
     * =====================================
     * 
     * Windows communicates with programs through messages:
     * 
     * WM_PAINT:   Window needs to be drawn
     * WM_TIMER:   Timer expired
     * WM_KEYDOWN: Key was pressed
     * WM_DESTROY: Window being destroyed
     * 
     * The WindowProc handles these messages.
     * It's a static function (required by Win32), so we pass 'this'
     * through window user data to access instance members.
     */
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        RecoveryOverlay* self = nullptr;
        
        if (msg == WM_CREATE) {
            // Store 'this' pointer in window user data
            CREATESTRUCTA* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
            self = reinterpret_cast<RecoveryOverlay*>(cs->lpCreateParams);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<RecoveryOverlay*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        }
        
        if (!self) {
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
        
        switch (msg) {
            case WM_PAINT:
                self->paint();
                return 0;
                
            case WM_TIMER:
                if (wParam == TIMER_AUTO_DISMISS) {
                    self->hide();
                    if (self->on_dismiss_) self->on_dismiss_();
                }
                return 0;
                
            case WM_KEYDOWN:
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
                // Any input dismisses the overlay
                self->hide();
                if (self->on_dismiss_) self->on_dismiss_();
                return 0;
                
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
        }
        
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    
    /**
     * Paint the overlay content
     * 
     * EDUCATIONAL NOTE: GDI Drawing
     * ==============================
     * 
     * Windows GDI (Graphics Device Interface) draws using:
     * - Device Context (DC): Canvas to draw on
     * - Brushes: Fill shapes
     * - Pens: Draw lines
     * - Fonts: Draw text
     * 
     * BeginPaint/EndPaint bracket drawing operations.
     * Select objects into DC before use, restore when done.
     */
    void paint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        
        RECT rect;
        GetClientRect(hwnd_, &rect);
        
        // Create background brush
        HBRUSH bg_brush = CreateSolidBrush(config_.background);
        FillRect(hdc, &rect, bg_brush);
        DeleteObject(bg_brush);
        
        // Set text properties
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, config_.text_color);
        
        int y = 20;  // Starting Y position
        int padding = 20;
        
        // Title: "Welcome back! You were:"
        HFONT old_font = (HFONT)SelectObject(hdc, font_title_);
        SetTextColor(hdc, config_.accent);
        
        const char* title = "🧠 Welcome back! You were:";
        TextOutA(hdc, padding, y, title, (int)strlen(title));
        y += 35;
        
        // Main context
        SelectObject(hdc, font_body_);
        SetTextColor(hdc, config_.text_color);
        
        // File/context
        if (strlen(context_.last_productive.file_path) > 0) {
            char buffer[256];
            if (context_.last_productive.line_number > 0) {
                snprintf(buffer, sizeof(buffer), "📄 %s:%u",
                        context_.last_productive.file_path,
                        context_.last_productive.line_number);
            } else {
                snprintf(buffer, sizeof(buffer), "📄 %s",
                        context_.last_productive.file_path);
            }
            TextOutA(hdc, padding, y, buffer, (int)strlen(buffer));
            y += 25;
        }
        
        // Project
        if (strlen(context_.last_productive.project_name) > 0) {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "📁 Project: %s",
                    context_.last_productive.project_name);
            TextOutA(hdc, padding, y, buffer, (int)strlen(buffer));
            y += 25;
        }
        
        // Focus duration
        {
            char buffer[128];
            uint32_t minutes = context_.focus_duration_before_s / 60;
            if (minutes > 0) {
                snprintf(buffer, sizeof(buffer), "⏱️  Focused for: %u minutes", minutes);
            } else {
                snprintf(buffer, sizeof(buffer), "⏱️  Focused for: %u seconds", 
                        context_.focus_duration_before_s);
            }
            TextOutA(hdc, padding, y, buffer, (int)strlen(buffer));
            y += 25;
        }
        
        // Distraction info
        if (context_.distraction_duration_s > 0) {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "⚠️  Distracted for: %u seconds in %s",
                    context_.distraction_duration_s,
                    context_.distraction_app);
            SetTextColor(hdc, RGB(255, 150, 150));  // Light red
            TextOutA(hdc, padding, y, buffer, (int)strlen(buffer));
            SetTextColor(hdc, config_.text_color);
            y += 35;
        }
        
        // Recent activities
        if (context_.activity_count > 0) {
            const char* header = "Recent activity:";
            TextOutA(hdc, padding, y, header, (int)strlen(header));
            y += 22;
            
            for (size_t i = 0; i < context_.activity_count; i++) {
                char buffer[256];
                snprintf(buffer, sizeof(buffer), "  • %s",
                        context_.recent_activities[i].description);
                TextOutA(hdc, padding, y, buffer, (int)strlen(buffer));
                y += 20;
            }
        }
        
        // Dismiss hint (at bottom)
        SetTextColor(hdc, RGB(128, 128, 128));  // Gray
        const char* hint = "[Press any key to dismiss]";
        RECT hint_rect = rect;
        hint_rect.bottom -= 15;
        DrawTextA(hdc, hint, -1, &hint_rect, 
                  DT_CENTER | DT_BOTTOM | DT_SINGLELINE);
        
        SelectObject(hdc, old_font);
        EndPaint(hwnd_, &ps);
    }
};

#else
// Non-Windows placeholder
class RecoveryOverlay {
public:
    bool create(const OverlayConfig& = {}) { return false; }
    void show(const RecoveryContext&) {}
    void hide() {}
    bool is_visible() const { return false; }
    void destroy() {}
};
#endif // _WIN32

#endif // NEUROFOCUS_OVERLAY_H
