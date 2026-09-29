// drawing_lib.cpp — Drawing.new(kind) with property tables per object
// language: C++20, target: Windows 11 x64, MSVC
//
// exposes:
//   Drawing.new("Line" | "Text" | "Circle" | "Square" | "Quad" | "Triangle" | "Image")
//   Drawing.Fonts.{UI, System, Plex, Monospace}
//   Drawing.Clear()  — wipe all live objects
//
// each object has a property table with named fields (Visible, Color,
// Thickness, From, To, Position, Size, Radius, NumSides, Filled, Text,
// TextBounds, ZIndex, Transparency). setting a property mutates state; on
// every RunService.RenderStepped a boot Luau tick calls r9k._drawing_render
// which walks live objects and issues draws.
//
// Color3 handling: we accept either { R, G, B } table (0..1 range) or a
// Roblox Color3 userdata. transparency 0..1 (Roblox convention where 1 =
// invisible) is inverted to alpha 0..255.
#include "env/drawing_lib.h"
#include "env/environment.h"
#include "luau/api.h"
#include "luau/internal.h"
#include "ui/renderer.h"

namespace r9k::env::drawing_lib {

using luau::api;

namespace {
    enum class Kind { Line, Text, Circle, Square, Quad, Triangle, Image };

    struct Obj {
        Kind  kind;
        bool  visible{true};
        bool  filled{false};
        float thickness{1.f};
        float transparency{0.f};
        int   zindex{0};
        int   sides{24};
        float radius{10.f};
        float x1{}, y1{}, x2{}, y2{}, x3{}, y3{}, x4{}, y4{};
        float pos_x{}, pos_y{}, size_x{}, size_y{};
        ui::DrawColor color{255,255,255,255};
        std::string text;
        int text_size{14};
    };

    std::mutex          g_mtx;
    std::vector<Obj*>   g_live;
}

static Obj* obj_at(lua_State* L, int idx) {
    const auto& a = api();
    if (a.type(L, idx) != luau::LUA_TUSERDATA && a.type(L, idx) != luau::LUA_TTABLE) return nullptr;
    a.getfield(L, idx, "_p");
    if (a.type(L, -1) != luau::LUA_TLIGHTUSERDATA) { a.settop(L, -2); return nullptr; }
    auto* p = (Obj*)a.touserdata(L, -1);
    a.settop(L, -2);
    return p;
}

// __newindex : object[key] = value → mutate underlying Obj
static int obj_newindex(lua_State* L) {
    const auto& a = api();
    auto* o = obj_at(L, 1);
    if (!o) return 0;
    size_t klen=0;
    const char* k = a.tolstring(L, 2, &klen);
    if (!k) return 0;
    std::string_view key(k, klen);

    if      (key == "Visible")      o->visible      = a.toboolean(L, 3) != 0;
    else if (key == "Filled")       o->filled       = a.toboolean(L, 3) != 0;
    else if (key == "Thickness")    o->thickness    = (float)a.tonumberx(L, 3, nullptr);
    else if (key == "Transparency") o->transparency = (float)a.tonumberx(L, 3, nullptr);
    else if (key == "ZIndex")       o->zindex       = a.tointegerx(L, 3, nullptr);
    else if (key == "NumSides")     o->sides        = a.tointegerx(L, 3, nullptr);
    else if (key == "Radius")       o->radius       = (float)a.tonumberx(L, 3, nullptr);
    else if (key == "Text") { size_t n=0; auto s = a.tolstring(L, 3, &n); if (s) o->text.assign(s, n); }
    else if (key == "TextSize")     o->text_size    = a.tointegerx(L, 3, nullptr);
    else if (key == "Color") {
        // table form { R, G, B } or Color3 userdata — read fields
        a.getfield(L, 3, "R"); float r = (float)a.tonumberx(L, -1, nullptr); a.settop(L, -2);
        a.getfield(L, 3, "G"); float g = (float)a.tonumberx(L, -1, nullptr); a.settop(L, -2);
        a.getfield(L, 3, "B"); float b = (float)a.tonumberx(L, -1, nullptr); a.settop(L, -2);
        // fall back to array-style if fields empty
        if (r == 0 && g == 0 && b == 0) {
            a.rawgeti(L, 3, 1); r = (float)a.tonumberx(L, -1, nullptr); a.settop(L, -2);
            a.rawgeti(L, 3, 2); g = (float)a.tonumberx(L, -1, nullptr); a.settop(L, -2);
            a.rawgeti(L, 3, 3); b = (float)a.tonumberx(L, -1, nullptr); a.settop(L, -2);
        }
        o->color.r = (u8)(r * 255); o->color.g = (u8)(g * 255); o->color.b = (u8)(b * 255);
    }
    else if (key == "From")     { a.getfield(L, 3, "X"); o->x1 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); a.getfield(L, 3, "Y"); o->y1 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); }
    else if (key == "To")       { a.getfield(L, 3, "X"); o->x2 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); a.getfield(L, 3, "Y"); o->y2 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); }
    else if (key == "PointA")   { a.getfield(L, 3, "X"); o->x1 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); a.getfield(L, 3, "Y"); o->y1 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); }
    else if (key == "PointB")   { a.getfield(L, 3, "X"); o->x2 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); a.getfield(L, 3, "Y"); o->y2 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); }
    else if (key == "PointC")   { a.getfield(L, 3, "X"); o->x3 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); a.getfield(L, 3, "Y"); o->y3 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); }
    else if (key == "PointD")   { a.getfield(L, 3, "X"); o->x4 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); a.getfield(L, 3, "Y"); o->y4 = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); }
    else if (key == "Position") { a.getfield(L, 3, "X"); o->pos_x = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); a.getfield(L, 3, "Y"); o->pos_y = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); }
    else if (key == "Size")     { a.getfield(L, 3, "X"); o->size_x = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); a.getfield(L, 3, "Y"); o->size_y = (float)a.tonumberx(L, -1, nullptr); a.settop(L,-2); }
    return 0;
}

// Remove(self) — take off render list + free
static int obj_remove(lua_State* L) {
    const auto& a = api();
    auto* o = obj_at(L, 1);
    if (!o) return 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    for (auto it = g_live.begin(); it != g_live.end(); ++it) {
        if (*it == o) { g_live.erase(it); break; }
    }
    delete o;
    return 0;
}

static int obj_index(lua_State* L) {
    const auto& a = api();
    size_t klen=0;
    const char* k = a.tolstring(L, 2, &klen);
    if (!k) { a.pushnil(L); return 1; }
    std::string_view key(k, klen);
    if (key == "Remove" || key == "Destroy") {
        a.pushcclosure(L, obj_remove, "Drawing.Remove", 0);
        return 1;
    }
    auto* o = obj_at(L, 1);
    if (!o) { a.pushnil(L); return 1; }
    if      (key == "Visible")   { a.pushboolean(L, o->visible); return 1; }
    else if (key == "ClassName") { a.pushstring(L, "Drawing");   return 1; }
    a.pushnil(L);
    return 1;
}

static int drawing_new(lua_State* L) {
    const auto& a = api();
    size_t klen=0;
    const char* kind_str = a.tolstring(L, 1, &klen);
    Kind k = Kind::Line;
    if (kind_str) {
        std::string_view s(kind_str, klen);
        if      (s == "Text")     k = Kind::Text;
        else if (s == "Circle")   k = Kind::Circle;
        else if (s == "Square")   k = Kind::Square;
        else if (s == "Quad")     k = Kind::Quad;
        else if (s == "Triangle") k = Kind::Triangle;
        else if (s == "Image")    k = Kind::Image;
    }
    auto* o = new Obj{};
    o->kind = k;
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_live.push_back(o);
    }

    // build proxy table with metatable {__index=obj_index, __newindex=obj_newindex}
    a.createtable(L, 0, 2);
    a.pushlightuserdata(L, o);
    a.setfield(L, -2, "_p");

    a.createtable(L, 0, 2);
    a.pushcclosure(L, obj_index,    "Drawing.__index",    0); a.setfield(L, -2, "__index");
    a.pushcclosure(L, obj_newindex, "Drawing.__newindex", 0); a.setfield(L, -2, "__newindex");
    a.setmetatable(L, -2);
    return 1;
}

static int drawing_clear(lua_State*) {
    std::lock_guard<std::mutex> lk(g_mtx);
    for (auto* o : g_live) delete o;
    g_live.clear();
    return 0;
}

// _drawing_render — walk live objects and issue draws. called from Luau via
// RenderStepped tick installed at boot.
static int drawing_render(lua_State*) {
    ui::frame_reset();
    std::lock_guard<std::mutex> lk(g_mtx);
    for (auto* o : g_live) {
        if (!o->visible) continue;
        u8 alpha = (u8)std::max(0.f, std::min(1.f, 1.f - o->transparency)) * 255;
        ui::DrawColor c = o->color; c.a = alpha;
        switch (o->kind) {
            case Kind::Line:
                ui::draw_line(o->x1, o->y1, o->x2, o->y2, o->thickness, c); break;
            case Kind::Circle:
                ui::draw_circle(o->pos_x, o->pos_y, o->radius, o->sides, c, o->filled); break;
            case Kind::Square:
                ui::draw_rect(o->pos_x, o->pos_y, o->size_x, o->size_y, c, o->filled); break;
            case Kind::Triangle:
                ui::draw_triangle(o->x1, o->y1, o->x2, o->y2, o->x3, o->y3, c, o->filled); break;
            case Kind::Quad:
                ui::draw_quad(o->x1, o->y1, o->x2, o->y2, o->x3, o->y3, o->x4, o->y4, c, o->filled); break;
            case Kind::Text:
                ui::draw_text(o->pos_x, o->pos_y, o->text.c_str(), c, (float)o->text_size); break;
            case Kind::Image:
                break;  // image path lands with ImGui font/texture stack
        }
    }
    return 0;
}

void install() {
    ui::renderer_install();

    register_lib("Drawing", {
        {"new",   drawing_new},
        {"Clear", drawing_clear},
    });
    register_fn("_drawing_render", drawing_render);
    register_fn("cleardrawcache",  drawing_clear);
}

}  // namespace r9k::env::drawing_lib
