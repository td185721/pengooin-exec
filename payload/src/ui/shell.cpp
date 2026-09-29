// shell.cpp — native Win32 executor shell
// language: C++20, target: Windows 11 x64, MSVC
//
// design:
//   - own top-level layered window (WS_EX_TOPMOST | WS_EX_LAYERED)
//   - tabs: Editor, Scripts, Console, Settings — implemented as a tab control
//     with child windows swapped by tab index
//   - editor: multi-line EDIT (ES_MULTILINE|ES_WANTRETURN|ES_AUTOVSCROLL)
//     with Consolas 11pt; input font monospaced. Ctrl+Enter also executes.
//   - Execute → hands buffer to luau::run on a worker thread so the UI stays
//     responsive; result piped to console tab via a mutex-guarded queue.
//   - Ctrl+Shift+P: global hotkey (WM_HOTKEY) toggles visibility.
#include "ui/shell.h"
#include "luau/executor.h"

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
    name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
    processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace r9k::ui {

namespace {
    HWND    g_hwnd    = nullptr;
    HWND    g_tabs    = nullptr;
    HWND    g_editor  = nullptr;
    HWND    g_console = nullptr;
    HWND    g_scripts = nullptr;
    HWND    g_btn_run = nullptr;
    HWND    g_btn_clear = nullptr;
    HWND    g_btn_save = nullptr;
    HFONT   g_font_mono = nullptr;
    HANDLE  g_thread  = nullptr;
    std::atomic<bool> g_running{false};
    std::atomic<bool> g_visible{true};

    std::mutex               g_out_mtx;
    std::vector<std::string> g_pending_out;

    constexpr int ID_TABS   = 1001;
    constexpr int ID_EDITOR = 1002;
    constexpr int ID_CONSOLE= 1003;
    constexpr int ID_SCRIPTS= 1004;
    constexpr int ID_RUN    = 1010;
    constexpr int ID_CLEAR  = 1011;
    constexpr int ID_SAVE   = 1012;
    constexpr int HOTKEY_TOGGLE = 0x9E01;

    void layout_children(HWND parent) {
        RECT rc; GetClientRect(parent, &rc);
        MoveWindow(g_tabs,   0,   0, rc.right,       rc.bottom - 40, TRUE);

        RECT tabrc; GetClientRect(g_tabs, &tabrc);
        TabCtrl_AdjustRect(g_tabs, FALSE, &tabrc);
        MoveWindow(g_editor,  tabrc.left,  tabrc.top, tabrc.right - tabrc.left, tabrc.bottom - tabrc.top, TRUE);
        MoveWindow(g_console, tabrc.left,  tabrc.top, tabrc.right - tabrc.left, tabrc.bottom - tabrc.top, TRUE);
        MoveWindow(g_scripts, tabrc.left,  tabrc.top, tabrc.right - tabrc.left, tabrc.bottom - tabrc.top, TRUE);

        MoveWindow(g_btn_run,   rc.right - 320, rc.bottom - 34, 100, 28, TRUE);
        MoveWindow(g_btn_clear, rc.right - 216, rc.bottom - 34, 100, 28, TRUE);
        MoveWindow(g_btn_save,  rc.right - 112, rc.bottom - 34, 100, 28, TRUE);
    }

    void show_tab(int idx) {
        ShowWindow(g_editor,  idx == 0 ? SW_SHOW : SW_HIDE);
        ShowWindow(g_console, idx == 1 ? SW_SHOW : SW_HIDE);
        ShowWindow(g_scripts, idx == 2 ? SW_SHOW : SW_HIDE);
    }

    void console_append(std::string_view line) {
        int len = GetWindowTextLengthA(g_console);
        SendMessageA(g_console, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        std::string ln(line);
        ln += "\r\n";
        SendMessageA(g_console, EM_REPLACESEL, FALSE, (LPARAM)ln.c_str());
    }

    void execute_current() {
        int len = GetWindowTextLengthA(g_editor);
        std::string buf(len, '\0');
        GetWindowTextA(g_editor, buf.data(), len + 1);
        buf.resize(len);

        std::thread([b = std::move(buf)]() {
            auto r = r9k::luau::run(b, "=[r9k.editor]");
            std::lock_guard<std::mutex> lk(g_out_mtx);
            g_pending_out.push_back(r.ok ? "[ok]" : ("[err] " + r.error));
        }).detach();
    }

    void drain_console() {
        std::vector<std::string> pending;
        {
            std::lock_guard<std::mutex> lk(g_out_mtx);
            pending.swap(g_pending_out);
        }
        for (auto& s : pending) console_append(s);
    }

    LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES };
            InitCommonControlsEx(&icc);

            g_font_mono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                FF_MODERN | FIXED_PITCH, L"Consolas");

            g_tabs = CreateWindowW(WC_TABCONTROLW, L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_TABS, nullptr, nullptr);
            TCITEMW ti{};
            ti.mask = TCIF_TEXT;
            ti.pszText = (LPWSTR)L"Editor";  TabCtrl_InsertItem(g_tabs, 0, &ti);
            ti.pszText = (LPWSTR)L"Console"; TabCtrl_InsertItem(g_tabs, 1, &ti);
            ti.pszText = (LPWSTR)L"Scripts"; TabCtrl_InsertItem(g_tabs, 2, &ti);

            g_editor = CreateWindowW(L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_EDITOR, nullptr, nullptr);
            g_console = CreateWindowW(L"EDIT", L"",
                WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_CONSOLE, nullptr, nullptr);
            g_scripts = CreateWindowW(L"LISTBOX", L"",
                WS_CHILD | WS_VSCROLL | LBS_STANDARD,
                0, 0, 0, 0, h, (HMENU)(uintptr_t)ID_SCRIPTS, nullptr, nullptr);

            for (HWND w : {g_editor, g_console, g_scripts})
                SendMessage(w, WM_SETFONT, (WPARAM)g_font_mono, TRUE);

            g_btn_run   = CreateWindowW(L"BUTTON", L"Execute",  WS_CHILD | WS_VISIBLE, 0,0,0,0, h, (HMENU)(uintptr_t)ID_RUN,   nullptr, nullptr);
            g_btn_clear = CreateWindowW(L"BUTTON", L"Clear",    WS_CHILD | WS_VISIBLE, 0,0,0,0, h, (HMENU)(uintptr_t)ID_CLEAR, nullptr, nullptr);
            g_btn_save  = CreateWindowW(L"BUTTON", L"Save",     WS_CHILD | WS_VISIBLE, 0,0,0,0, h, (HMENU)(uintptr_t)ID_SAVE,  nullptr, nullptr);

            show_tab(0);
            SetTimer(h, 1, 100, nullptr);  // console drain tick
            RegisterHotKey(h, HOTKEY_TOGGLE, MOD_CONTROL | MOD_SHIFT, 'P');
            return 0;
        }
        case WM_SIZE:
            layout_children(h);
            return 0;
        case WM_NOTIFY: {
            auto* nm = (LPNMHDR)l;
            if (nm->idFrom == ID_TABS && nm->code == TCN_SELCHANGE)
                show_tab(TabCtrl_GetCurSel(g_tabs));
            return 0;
        }
        case WM_COMMAND: {
            switch (LOWORD(w)) {
            case ID_RUN:
                execute_current();
                TabCtrl_SetCurSel(g_tabs, 1); show_tab(1);
                return 0;
            case ID_CLEAR:
                if (TabCtrl_GetCurSel(g_tabs) == 1) SetWindowTextA(g_console, "");
                else SetWindowTextA(g_editor, "");
                return 0;
            case ID_SAVE: {
                int len = GetWindowTextLengthA(g_editor);
                std::string buf(len, '\0');
                GetWindowTextA(g_editor, buf.data(), len + 1);
                buf.resize(len);
                wchar_t path[MAX_PATH]; GetTempPathW(MAX_PATH, path);
                wcscat_s(path, MAX_PATH, L"pengooin_last.luau");
                HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD wr; WriteFile(f, buf.data(), (DWORD)buf.size(), &wr, nullptr);
                    CloseHandle(f);
                }
                return 0;
            }
            }
            return 0;
        }
        case WM_TIMER:
            drain_console();
            return 0;
        case WM_HOTKEY:
            if (w == HOTKEY_TOGGLE) shell_toggle();
            return 0;
        case WM_CLOSE:
            ShowWindow(h, SW_HIDE);
            g_visible = false;
            return 0;
        case WM_DESTROY:
            KillTimer(h, 1);
            UnregisterHotKey(h, HOTKEY_TOGGLE);
            if (g_font_mono) DeleteObject(g_font_mono);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }

    DWORD WINAPI thread_main(LPVOID) {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = proc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"r9k_shell";
        RegisterClassW(&wc);

        g_hwnd = CreateWindowExW(WS_EX_TOPMOST, L"r9k_shell", L"pengooin",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
            nullptr, nullptr, wc.hInstance, nullptr);
        if (!g_hwnd) return 1;
        ShowWindow(g_hwnd, SW_SHOW);
        UpdateWindow(g_hwnd);
        g_visible = true;

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        g_running = false;
        return 0;
    }
}

void shell_start() {
    if (g_running.exchange(true)) return;
    g_thread = CreateThread(nullptr, 0, thread_main, nullptr, 0, nullptr);
}

void shell_stop() {
    if (!g_running) return;
    if (g_hwnd) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    if (g_thread) { WaitForSingleObject(g_thread, 2000); CloseHandle(g_thread); g_thread = nullptr; }
}

void shell_toggle() {
    if (!g_hwnd) return;
    if (g_visible) { ShowWindow(g_hwnd, SW_HIDE); g_visible = false; }
    else           { ShowWindow(g_hwnd, SW_SHOW); SetForegroundWindow(g_hwnd); g_visible = true; }
}

}  // namespace r9k::ui
