// input_lib.cpp — mouse + keyboard synthesis
// language: C++20, target: Windows 11 x64, MSVC
//
// exposes:
//   mouse1click / mouse1press / mouse1release
//   mouse2click / mouse2press / mouse2release
//   mousemoverel(dx, dy) / mousemoveabs(x, y)
//   mousescroll(amount)
//   keypress(vk) / keyrelease(vk) / keytap(vk)  — via SendInput
//   iskeydown(vk)                                — GetAsyncKeyState
//   getmouseloc() / setmouseloc()                — screen-space
//
// dispatch uses SendInput with MOUSEEVENTF_* + KEYEVENTF_* flags. requires
// the Roblox window to be foreground (Roblox filters background input for
// its own UIS pipeline). scan-code path used for keyboard so keybindings
// that read scancodes (games) receive them correctly.
#include "env/input_lib.h"
#include "env/environment.h"
#include "luau/api.h"
#include "luau/internal.h"

namespace r9k::env::input_lib {

using luau::api;

namespace {
    void send_mouse(DWORD flags, LONG dx = 0, LONG dy = 0, DWORD data = 0) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags   = flags;
        in.mi.dx        = dx;
        in.mi.dy        = dy;
        in.mi.mouseData = data;
        SendInput(1, &in, sizeof(in));
    }
    void send_key(u16 vk, bool up) {
        INPUT in{};
        in.type       = INPUT_KEYBOARD;
        in.ki.wVk     = vk;
        in.ki.wScan   = (u16)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
        SendInput(1, &in, sizeof(in));
    }
}

// mouse buttons
static int l_mouse1press  (lua_State*) { send_mouse(MOUSEEVENTF_LEFTDOWN);  return 0; }
static int l_mouse1release(lua_State*) { send_mouse(MOUSEEVENTF_LEFTUP);    return 0; }
static int l_mouse1click  (lua_State*) { send_mouse(MOUSEEVENTF_LEFTDOWN); send_mouse(MOUSEEVENTF_LEFTUP); return 0; }
static int l_mouse2press  (lua_State*) { send_mouse(MOUSEEVENTF_RIGHTDOWN); return 0; }
static int l_mouse2release(lua_State*) { send_mouse(MOUSEEVENTF_RIGHTUP);   return 0; }
static int l_mouse2click  (lua_State*) { send_mouse(MOUSEEVENTF_RIGHTDOWN); send_mouse(MOUSEEVENTF_RIGHTUP); return 0; }

// motion
static int l_mousemoverel(lua_State* L) {
    const auto& a = api();
    int dx = a.tointegerx(L, 1, nullptr);
    int dy = a.tointegerx(L, 2, nullptr);
    send_mouse(MOUSEEVENTF_MOVE, dx, dy);
    return 0;
}
static int l_mousemoveabs(lua_State* L) {
    const auto& a = api();
    int x = a.tointegerx(L, 1, nullptr);
    int y = a.tointegerx(L, 2, nullptr);
    // SendInput absolute uses 0..65535 across virtual screen
    int sw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int sh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (sw <= 0) sw = 1; if (sh <= 0) sh = 1;
    LONG nx = (LONG)((LONG64)x * 65535 / sw);
    LONG ny = (LONG)((LONG64)y * 65535 / sh);
    send_mouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, nx, ny);
    return 0;
}
static int l_mousescroll(lua_State* L) {
    const auto& a = api();
    int amt = a.tointegerx(L, 1, nullptr);
    send_mouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(amt * WHEEL_DELTA));
    return 0;
}

// cursor accessors
static int l_getmouseloc(lua_State* L) {
    const auto& a = api();
    POINT p{};
    GetCursorPos(&p);
    a.createtable(L, 0, 2);
    a.pushinteger(L, p.x); a.setfield(L, -2, "X");
    a.pushinteger(L, p.y); a.setfield(L, -2, "Y");
    return 1;
}
static int l_setmouseloc(lua_State* L) {
    const auto& a = api();
    int x = a.tointegerx(L, 1, nullptr);
    int y = a.tointegerx(L, 2, nullptr);
    SetCursorPos(x, y);
    return 0;
}

// keyboard
static int l_keypress(lua_State* L) {
    const auto& a = api();
    int vk = a.tointegerx(L, 1, nullptr);
    send_key((u16)vk, false);
    return 0;
}
static int l_keyrelease(lua_State* L) {
    const auto& a = api();
    int vk = a.tointegerx(L, 1, nullptr);
    send_key((u16)vk, true);
    return 0;
}
static int l_keytap(lua_State* L) {
    const auto& a = api();
    int vk = a.tointegerx(L, 1, nullptr);
    send_key((u16)vk, false);
    send_key((u16)vk, true);
    return 0;
}
static int l_iskeydown(lua_State* L) {
    const auto& a = api();
    int vk = a.tointegerx(L, 1, nullptr);
    a.pushboolean(L, (GetAsyncKeyState(vk) & 0x8000) != 0 ? 1 : 0);
    return 1;
}

void install() {
    register_fn("mouse1click",   l_mouse1click);
    register_fn("mouse1press",   l_mouse1press);
    register_fn("mouse1release", l_mouse1release);
    register_fn("mouse2click",   l_mouse2click);
    register_fn("mouse2press",   l_mouse2press);
    register_fn("mouse2release", l_mouse2release);
    register_fn("mousemoverel",  l_mousemoverel);
    register_fn("mousemoveabs",  l_mousemoveabs);
    register_fn("mousescroll",   l_mousescroll);
    register_fn("getmouselocation", l_getmouseloc);
    register_fn("setmouselocation", l_setmouseloc);
    register_fn("keypress",      l_keypress);
    register_fn("keyrelease",    l_keyrelease);
    register_fn("keytap",        l_keytap);
    register_fn("iskeydown",     l_iskeydown);
    register_fn("iscursordown",  l_iskeydown);
}

}  // namespace r9k::env::input_lib
