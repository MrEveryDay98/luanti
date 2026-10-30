// NetHack Voxel
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "nhv_levelready.h"
#include "nhv_camera.h"
#include "client/sound/sam_voice.h"
#include "client/hud.h"
#include "server.h"
#include "mapblock.h"
#include "lua_api/l_nethack.h"
#include "lua_api/l_internal.h"
#include "common/c_converter.h"
#include "log.h"
#include "porting.h"
#include "filesys.h"

#include "nhv_api.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <string>
#include <vector>

/*
 * nethack.dll is loaded at runtime (not linked) so that every new game
 * gets a fresh copy of NetHack's global state: core.nh_unload() frees the
 * library once a game has ended and core.nh_start() loads it again.
 */
namespace {

struct NhvFuncs {
#define NHV_FN(name) decltype(&::name) name = nullptr;
	NHV_FN(nhv_api_version)
	NHV_FN(nhv_start)
	NHV_FN(nhv_state)
	NHV_FN(nhv_wait)
	NHV_FN(nhv_exit_code)
	NHV_FN(nhv_get_request)
	NHV_FN(nhv_get_menu_item)
	NHV_FN(nhv_send_key)
	NHV_FN(nhv_send_text)
	NHV_FN(nhv_send_menu)
	NHV_FN(nhv_clear_input)
	NHV_FN(nhv_poll_event)
	NHV_FN(nhv_get_map)
	NHV_FN(nhv_get_monsters)
	NHV_FN(nhv_get_hero)
	NHV_FN(nhv_get_inventory)
	NHV_FN(nhv_get_floor_objects)
	NHV_FN(nhv_get_container)
	NHV_FN(nhv_jump_squares)
	NHV_FN(nhv_get_glyph_info)
	NHV_FN(nhv_max_glyph)
	NHV_FN(nhv_tile_count)
	NHV_FN(nhv_cmap_name)
	NHV_FN(nhv_cmap_tile)
	NHV_FN(nhv_mon_name)
	NHV_FN(nhv_obj_name)
	NHV_FN(nhv_set_freemove)
	NHV_FN(nhv_set_monstates)
	NHV_FN(nhv_get_all_monsters)
	NHV_FN(nhv_get_bubbles)
	NHV_FN(nhv_move_monster)
	NHV_FN(nhv_num_monsters)
	NHV_FN(nhv_mon_info)
	NHV_FN(nhv_get_missiles)
	NHV_FN(nhv_clear_missiles)
	NHV_FN(nhv_get_lights)
	NHV_FN(nhv_get_ball)
	NHV_FN(nhv_get_rays)
	NHV_FN(nhv_clear_rays)
	NHV_FN(nhv_get_spells)
	NHV_FN(nhv_get_shops)
	NHV_FN(nhv_game_id)
	NHV_FN(nhv_get_dungeons)
	NHV_FN(nhv_level_proto)
#undef NHV_FN
};

NhvFuncs nh;
#ifdef _WIN32
HMODULE nh_module = nullptr;
#endif

std::string dll_path()
{
#ifdef _WIN32
	wchar_t buf[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
	std::wstring w(buf, n);
	size_t slash = w.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		w = w.substr(0, slash + 1);
	w += L"nethack.dll";
	int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string s(len > 0 ? len - 1 : 0, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), len, nullptr, nullptr);
	return s;
#else
	return "";
#endif
}

bool load_library(std::string &err)
{
#ifdef _WIN32
	if (nh_module)
		return true;
	std::string path = dll_path();
	nh_module = LoadLibraryA(path.c_str());
	if (!nh_module) {
		err = "could not load " + path;
		return false;
	}
#define NHV_FN(name) \
	nh.name = reinterpret_cast<decltype(nh.name)>( \
		reinterpret_cast<void *>(GetProcAddress(nh_module, #name))); \
	if (!nh.name) { err = "nethack.dll lacks " #name; FreeLibrary(nh_module); \
		nh_module = nullptr; return false; }
	NHV_FN(nhv_api_version)
	NHV_FN(nhv_start)
	NHV_FN(nhv_state)
	NHV_FN(nhv_wait)
	NHV_FN(nhv_exit_code)
	NHV_FN(nhv_get_request)
	NHV_FN(nhv_get_menu_item)
	NHV_FN(nhv_send_key)
	NHV_FN(nhv_send_text)
	NHV_FN(nhv_send_menu)
	NHV_FN(nhv_clear_input)
	NHV_FN(nhv_poll_event)
	NHV_FN(nhv_get_map)
	NHV_FN(nhv_get_monsters)
	NHV_FN(nhv_get_hero)
	NHV_FN(nhv_get_inventory)
	NHV_FN(nhv_get_floor_objects)
	NHV_FN(nhv_get_container)
	NHV_FN(nhv_jump_squares)
	NHV_FN(nhv_get_glyph_info)
	NHV_FN(nhv_max_glyph)
	NHV_FN(nhv_tile_count)
	NHV_FN(nhv_cmap_name)
	NHV_FN(nhv_cmap_tile)
	NHV_FN(nhv_mon_name)
	NHV_FN(nhv_obj_name)
	NHV_FN(nhv_set_freemove)
	NHV_FN(nhv_set_monstates)
	NHV_FN(nhv_get_all_monsters)
	NHV_FN(nhv_get_bubbles)
	NHV_FN(nhv_move_monster)
	NHV_FN(nhv_num_monsters)
	NHV_FN(nhv_mon_info)
	NHV_FN(nhv_get_missiles)
	NHV_FN(nhv_clear_missiles)
	NHV_FN(nhv_get_lights)
	NHV_FN(nhv_get_ball)
	NHV_FN(nhv_get_rays)
	NHV_FN(nhv_clear_rays)
	NHV_FN(nhv_get_spells)
	NHV_FN(nhv_get_shops)
	NHV_FN(nhv_game_id)
	NHV_FN(nhv_get_dungeons)
	NHV_FN(nhv_level_proto)
#undef NHV_FN
	if (nh.nhv_api_version() != NHV_API_VERSION) {
		err = "nethack.dll API version mismatch";
		FreeLibrary(nh_module);
		nh_module = nullptr;
		return false;
	}
	return true;
#else
	err = "nethack.dll is only supported on Windows";
	return false;
#endif
}

bool loaded()
{
#ifdef _WIN32
	return nh_module != nullptr;
#else
	return false;
#endif
}

int state_or_none()
{
	return loaded() ? nh.nhv_state() : NHV_NOT_STARTED;
}

const char *state_name(int st)
{
	switch (st) {
	case NHV_BUSY: return "busy";
	case NHV_WAITING: return "waiting";
	case NHV_ENDED: return "ended";
	default: return "not_started";
	}
}

const char *req_kind_name(int k)
{
	switch (k) {
	case NHV_REQ_KEY: return "key";
	case NHV_REQ_YN: return "yn";
	case NHV_REQ_MENU: return "menu";
	case NHV_REQ_LINE: return "line";
	case NHV_REQ_EXTCMD: return "extcmd";
	case NHV_REQ_POS: return "pos";
	case NHV_REQ_TURN: return "turn";
	default: return "none";
	}
}

const char *event_kind_name(int k)
{
	switch (k) {
	case NHV_EV_MESSAGE: return "message";
	case NHV_EV_TEXT_BEGIN: return "text_begin";
	case NHV_EV_TEXT_LINE: return "text_line";
	case NHV_EV_TEXT_END: return "text_end";
	case NHV_EV_RAW: return "raw";
	case NHV_EV_LEVEL_CHANGE: return "level_change";
	case NHV_EV_BELL: return "bell";
	case NHV_EV_DELAY: return "delay";
	case NHV_EV_MISSILE: return "missile";
	case NHV_EV_COMBAT: return "combat";
	case NHV_EV_SOUND: return "sound";
	case NHV_EV_RAY: return "ray";
	case NHV_EV_CONTAINER: return "container";
	case NHV_EV_SPLEV: return "splev";
	case NHV_EV_ACHIEVE: return "achieve";
	default: return "unknown";
	}
}

const char *glyph_kind_name(int k)
{
	static const char *names[] = {"none", "mon", "pet", "invis", "detected",
		"body", "ridden", "obj", "cmap", "explode", "zap", "swallow",
		"warning", "statue", "unexplored", "nothing"};
	if (k < 0 || k >= (int)(sizeof(names) / sizeof(names[0])))
		return "none";
	return names[k];
}

void set_int(lua_State *L, const char *k, lua_Integer v)
{
	lua_pushinteger(L, v);
	lua_setfield(L, -2, k);
}

void set_str(lua_State *L, const char *k, const char *v)
{
	lua_pushstring(L, v);
	lua_setfield(L, -2, k);
}

void push_obj(lua_State *L, const nhv_obj &o)
{
	lua_createtable(L, 0, 10);
	set_int(L, "id", o.id);
	char let[2] = {o.invlet, 0};
	set_str(L, "invlet", let);
	set_int(L, "oclass", o.oclass);
	set_int(L, "otyp", o.otyp);
	set_int(L, "quan", o.quan);
	set_int(L, "glyph", o.glyph);
	set_int(L, "tile", o.tile);
	set_int(L, "flags", o.flags);
	set_int(L, "weight", o.weight);
	set_int(L, "worn", o.worn);
	set_int(L, "armcat", o.armcat);
	set_str(L, "name", o.name);
}

#define REQUIRE_LOADED() \
	do { if (!loaded()) { lua_pushnil(L); return 1; } } while (0)

} // namespace

// nh_start(datadir, userdir, options, name) -> true | nil, err
int ModApiNetHack::l_nh_start(lua_State *L)
{
	std::string datadir = readParam<std::string>(L, 1);
	std::string userdir = readParam<std::string>(L, 2);
	std::string options = lua_isstring(L, 3) ? readParam<std::string>(L, 3) : "";
	std::string name = lua_isstring(L, 4) ? readParam<std::string>(L, 4) : "";

	std::string err;
	if (!load_library(err)) {
		lua_pushnil(L);
		lua_pushstring(L, err.c_str());
		return 2;
	}
	if (nh.nhv_state() != NHV_NOT_STARTED) {
		lua_pushnil(L);
		lua_pushstring(L, "a game is already running");
		return 2;
	}
	fs::CreateAllDirs(userdir);
	int rc = nh.nhv_start(datadir.c_str(), userdir.c_str(), options.c_str(), name.c_str());
	if (rc != 0) {
		lua_pushnil(L);
		lua_pushstring(L, ("nhv_start failed: " + std::to_string(rc)).c_str());
		return 2;
	}
	lua_pushboolean(L, true);
	return 1;
}

// nh_unload() -> bool: frees nethack.dll; only allowed once the game ended
int ModApiNetHack::l_nh_unload(lua_State *L)
{
#ifdef _WIN32
	if (nh_module) {
		int st = nh.nhv_state();
		if (st == NHV_BUSY || st == NHV_WAITING) {
			lua_pushboolean(L, false);
			return 1;
		}
		FreeLibrary(nh_module);
		nh_module = nullptr;
		nh = NhvFuncs();
	}
#endif
	lua_pushboolean(L, true);
	return 1;
}

int ModApiNetHack::l_nh_state(lua_State *L)
{
	lua_pushstring(L, state_name(state_or_none()));
	return 1;
}

// nh_wait(timeout_ms) -> state name
int ModApiNetHack::l_nh_wait(lua_State *L)
{
	int ms = luaL_optinteger(L, 1, 0);
	int st = loaded() ? nh.nhv_wait(ms) : NHV_NOT_STARTED;
	lua_pushstring(L, state_name(st));
	return 1;
}

int ModApiNetHack::l_nh_exit_code(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushinteger(L, nh.nhv_exit_code());
	return 1;
}

// nh_request() -> {kind=, prompt=, choices=, default=, for_command=,
//                   menu_how=, items={{sel=, text=, selectable=, ...}}}
int ModApiNetHack::l_nh_request(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_request r;
	nh.nhv_get_request(&r);
	lua_createtable(L, 0, 8);
	set_str(L, "kind", req_kind_name(r.kind));
	set_str(L, "prompt", r.prompt);
	set_str(L, "choices", r.choices);
	char def[2] = {(char)r.def, 0};
	set_str(L, "default", def);
	lua_pushboolean(L, r.for_command);
	lua_setfield(L, -2, "for_command");
	set_int(L, "menu_how", r.menu_how);
	if (r.kind == NHV_REQ_MENU) {
		lua_createtable(L, r.menu_count, 0);
		for (int i = 0; i < r.menu_count; i++) {
			nhv_menu_item it;
			if (!nh.nhv_get_menu_item(i, &it))
				break;
			lua_createtable(L, 0, 7);
			char sel[2] = {it.sel, 0};
			set_str(L, "sel", sel);
			set_str(L, "text", it.text);
			lua_pushboolean(L, it.selectable);
			lua_setfield(L, -2, "selectable");
			lua_pushboolean(L, it.preselected);
			lua_setfield(L, -2, "preselected");
			set_int(L, "glyph", it.glyph);
			set_int(L, "tile", it.tile);
			set_int(L, "attr", it.attr);
			lua_rawseti(L, -2, i + 1);
		}
		lua_setfield(L, -2, "items");
	}
	return 1;
}

// nh_send_key(key): key is a one-char string or a char code
int ModApiNetHack::l_nh_send_key(lua_State *L)
{
	if (!loaded())
		return 0;
	int ch;
	if (lua_type(L, 1) == LUA_TNUMBER)
		ch = lua_tointeger(L, 1);
	else {
		std::string s = readParam<std::string>(L, 1);
		ch = s.empty() ? 27 : (unsigned char)s[0];
	}
	nh.nhv_send_key(ch);
	return 0;
}

int ModApiNetHack::l_nh_send_text(lua_State *L)
{
	if (!loaded())
		return 0;
	nh.nhv_send_text(readParam<std::string>(L, 1).c_str());
	return 0;
}

int ModApiNetHack::l_nh_send_menu(lua_State *L)
{
	if (!loaded())
		return 0;
	nh.nhv_send_menu(readParam<std::string>(L, 1).c_str());
	return 0;
}

int ModApiNetHack::l_nh_clear_input(lua_State *L)
{
	if (loaded())
		nh.nhv_clear_input();
	return 0;
}

// nh_poll_events() -> list of {kind=, text=, attr=}
int ModApiNetHack::l_nh_poll_events(lua_State *L)
{
	lua_newtable(L);
	if (!loaded())
		return 1;
	nhv_event ev;
	int i = 1;
	while (nh.nhv_poll_event(&ev)) {
		lua_createtable(L, 0, 3);
		set_str(L, "kind", event_kind_name(ev.kind));
		set_str(L, "text", ev.text);
		set_int(L, "attr", ev.attr);
		lua_rawseti(L, -2, i++);
	}
	return 1;
}

// nh_get_map() -> {glyph={}, mem={}, tile={}, mem_tile={}, typ={},
//                   flags={}, door={}, back={}} with index x*21 + y + 1
int ModApiNetHack::l_nh_get_map(lua_State *L)
{
	REQUIRE_LOADED();
	static std::vector<nhv_loc> buf(NHV_COLNO * NHV_ROWNO);
	nh.nhv_get_map(buf.data());
	const int n = NHV_COLNO * NHV_ROWNO;
	lua_createtable(L, 0, 9);
	set_int(L, "colno", NHV_COLNO);
	set_int(L, "rowno", NHV_ROWNO);
#define FIELD_ARRAY(name, expr) \
	lua_createtable(L, n, 0); \
	for (int i = 0; i < n; i++) { \
		const nhv_loc &l = buf[i]; \
		lua_pushinteger(L, (expr)); \
		lua_rawseti(L, -2, i + 1); \
	} \
	lua_setfield(L, -2, name);
	FIELD_ARRAY("glyph", l.glyph)
	FIELD_ARRAY("mem", l.mem_glyph)
	FIELD_ARRAY("tile", l.tile)
	FIELD_ARRAY("mem_tile", l.mem_tile)
	FIELD_ARRAY("typ", l.typ)
	FIELD_ARRAY("flags", l.flags)
	FIELD_ARRAY("door", l.door)
	FIELD_ARRAY("back", l.back)
	FIELD_ARRAY("trap", l.trap)
	FIELD_ARRAY("back_tile", l.back_tile)
#undef FIELD_ARRAY
	return 1;
}

int ModApiNetHack::l_nh_get_monsters(lua_State *L)
{
	REQUIRE_LOADED();
	static std::vector<nhv_mon> buf(512);
	int n = nh.nhv_get_monsters(buf.data(), (int)buf.size());
	if (n > (int)buf.size()) { // (more than that on the level: room for them all)
		buf.resize(n + 64);
		n = nh.nhv_get_monsters(buf.data(), (int)buf.size());
	}
	if (n > (int)buf.size())
		n = (int)buf.size();
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		const nhv_mon &m = buf[i];
		lua_createtable(L, 0, 8);
		set_int(L, "id", m.id);
		set_int(L, "x", m.x);
		set_int(L, "y", m.y);
		set_int(L, "glyph", m.glyph);
		set_int(L, "tile", m.tile);
		set_int(L, "flags", m.flags);
		set_int(L, "mndx", m.mndx);
		set_int(L, "size", m.size);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

int ModApiNetHack::l_nh_get_hero(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_hero h;
	nh.nhv_get_hero(&h);
	lua_createtable(L, 0, 40);
	set_int(L, "x", h.x); set_int(L, "y", h.y);
	set_int(L, "ux0", h.ux0); set_int(L, "uy0", h.uy0);
	set_int(L, "dnum", h.dnum); set_int(L, "dlevel", h.dlevel);
	set_int(L, "depth", h.depth); set_int(L, "ledger", h.ledger);
	set_int(L, "moves", h.moves);
	set_int(L, "hp", h.hp); set_int(L, "hpmax", h.hpmax);
	set_int(L, "pw", h.pw); set_int(L, "pwmax", h.pwmax);
	set_int(L, "ac", h.ac); set_int(L, "xlvl", h.xlvl);
	set_int(L, "xp", h.xp); set_int(L, "gold", h.gold);
	set_int(L, "str", h.str); set_int(L, "dex", h.dex);
	set_int(L, "con", h.con); set_int(L, "int", h.intl);
	set_int(L, "wis", h.wis); set_int(L, "cha", h.cha);
	set_int(L, "speed", h.speed); set_int(L, "hunger", h.hunger);
	set_int(L, "encumbrance", h.encumbrance);
	set_int(L, "glyph", h.glyph); set_int(L, "tile", h.tile);
	set_int(L, "mndx", h.mndx); set_int(L, "flags", h.flags);
	set_int(L, "conds", h.conds);
	set_str(L, "ability", h.ability);
	set_int(L, "dx", h.dx); set_int(L, "dy", h.dy);
	set_str(L, "name", h.name); set_str(L, "rank", h.rank);
	set_str(L, "align", h.align);
	set_str(L, "god", h.god);
	set_str(L, "dungeon_name", h.dungeon_name);
	set_str(L, "level_desc", h.level_desc);
	return 1;
}

int ModApiNetHack::l_nh_get_inventory(lua_State *L)
{
	REQUIRE_LOADED();
	static std::vector<nhv_obj> buf(128);
	int n = nh.nhv_get_inventory(buf.data(), (int)buf.size());
	if (n > (int)buf.size())
		n = (int)buf.size();
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		push_obj(L, buf[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

int ModApiNetHack::l_nh_get_floor_objects(lua_State *L)
{
	REQUIRE_LOADED();
	int x = luaL_checkinteger(L, 1), y = luaL_checkinteger(L, 2);
	static std::vector<nhv_obj> buf(64);
	int n = nh.nhv_get_floor_objects(x, y, buf.data(), (int)buf.size());
	if (n > (int)buf.size())
		n = (int)buf.size();
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		push_obj(L, buf[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// nh_jump_squares() -> list of {x=, y=} where a jump (#jump) could land
// now, and "ok"; or nil and "not_now" (#jump says why) / "no" (can't jump)
int ModApiNetHack::l_nh_jump_squares(lua_State *L)
{
	REQUIRE_LOADED();
	int xy[128];
	int n = nh.nhv_jump_squares(xy, 64);
	if (n < 0) {
		lua_pushnil(L);
		lua_pushstring(L, n == -1 ? "not_now" : "no");
		return 2;
	}
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		lua_createtable(L, 0, 2);
		lua_pushinteger(L, xy[2 * i]);
		lua_setfield(L, -2, "x");
		lua_pushinteger(L, xy[2 * i + 1]);
		lua_setfield(L, -2, "y");
		lua_rawseti(L, -2, i + 1);
	}
	lua_pushstring(L, "ok");
	return 2;
}

// nh_get_container(id) -> list of objects, or nil if it isn't reachable
int ModApiNetHack::l_nh_get_container(lua_State *L)
{
	REQUIRE_LOADED();
	unsigned id = (unsigned)luaL_checkinteger(L, 1);
	static std::vector<nhv_obj> buf(256);
	int n = nh.nhv_get_container(id, buf.data(), (int)buf.size());
	if (n < 0) {
		lua_pushnil(L);
		return 1;
	}
	if (n > (int)buf.size())
		n = (int)buf.size();
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		push_obj(L, buf[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

int ModApiNetHack::l_nh_glyph_info(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_glyph_info gi;
	nh.nhv_get_glyph_info(luaL_checkinteger(L, 1), &gi);
	lua_createtable(L, 0, 5);
	set_str(L, "kind", glyph_kind_name(gi.kind));
	set_int(L, "index", gi.index);
	set_int(L, "tile", gi.tile);
	set_int(L, "color", gi.color);
	set_int(L, "sym", gi.sym);
	return 1;
}

// nhv_camera() -> where the client's camera is (a vector, nodes), or nil
// (nhv_camera.h; not NetHack's: works with no game loaded)
int ModApiNetHack::l_nhv_camera(lua_State *L)
{
	v3f p;
	if (!nhv_camera::get(&p)) {
		lua_pushnil(L);
		return 1;
	}
	push_v3f(L, p);
	return 1;
}

// core.nh_get_bubbles() -> {{x=, y=, w=, h=}...}: the Plane of Water's air
// bubbles (their top left squares and sizes), in NetHack's own order
int ModApiNetHack::l_nh_get_bubbles(lua_State *L)
{
	REQUIRE_LOADED();
	static int buf[4 * 64];
	int n = nh.nhv_get_bubbles(buf, 64);
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		lua_createtable(L, 0, 4);
		set_int(L, "x", buf[4 * i]);
		set_int(L, "y", buf[4 * i + 1]);
		set_int(L, "w", buf[4 * i + 2]);
		set_int(L, "h", buf[4 * i + 3]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// core.nhv_camera_roll(degrees): tilt the view to the side (0: level); the
// camera eases there (nhv_camera.h)
int ModApiNetHack::l_nhv_camera_roll(lua_State *L)
{
	nhv_camera::set_roll((float)luaL_checknumber(L, 1));
	return 0;
}

// core.nhv_player_drift(velocity): carry the player along at this velocity
// (nodes per second; zero vector: stop) - client-side, every frame
// (nhv_camera.h)
int ModApiNetHack::l_nhv_player_drift(lua_State *L)
{
	nhv_camera::set_drift(read_v3f(L, 1));
	return 0;
}

// core.nhv_hero_light(level): the light the hero carries (a light level, 0
// none), lit round the player's head by the shaders (client/game.cpp; the
// client is in this same program)
void nhv_set_hero_light(float level);
int ModApiNetHack::l_nhv_hero_light(lua_State *L)
{
	nhv_set_hero_light((float)luaL_checknumber(L, 1));
	return 0;
}

// core.nhv_map_scroll(px, instant): the big map's pictures (HUD style 256)
// moved px pixels left, the client easing them there - at once if instant
// (client/hud.h; the client is in this same program)
int ModApiNetHack::l_nhv_map_scroll(lua_State *L)
{
	nhv_set_map_scroll((float)luaL_checknumber(L, 1), lua_toboolean(L, 2));
	return 0;
}

// core.nhv_sam_seconds(name): how long a SAM line ("nhv_sam:...|text", as
// played: sound/sam_voice.h) takes to say - rendered here, the client
// being in this same program - 0 if it can't be said
int ModApiNetHack::l_nhv_sam_seconds(lua_State *L)
{
	lua_pushnumber(L, sound::samVoiceSeconds(luaL_checkstring(L, 1)));
	return 1;
}

// core.nhv_camera_orbit({center=, radius=, height=, speed=, angle=,
// look_y=}): the camera circles center, looking at it, placed by the
// client every frame (nhv_camera.h); core.nhv_camera_orbit({look_y=}) while
// it goes round: look higher or lower; core.nhv_camera_orbit(nil): stop
int ModApiNetHack::l_nhv_camera_orbit(lua_State *L)
{
	if (!lua_istable(L, 1)) {
		nhv_camera::stop_orbit();
		return 0;
	}
	lua_getfield(L, 1, "center");
	bool start = lua_istable(L, -1);
	lua_pop(L, 1);
	auto num = [L](const char *k, float def) {
		lua_getfield(L, 1, k);
		float v = lua_isnumber(L, -1) ? (float)lua_tonumber(L, -1) : def;
		lua_pop(L, 1);
		return v;
	};
	if (!start) {
		nhv_camera::set_orbit_look(num("look_y", nhv_camera::get_orbit().look_y));
		return 0;
	}
	nhv_camera::Orbit o;
	lua_getfield(L, 1, "center");
	o.center = read_v3f(L, -1);
	lua_pop(L, 1);
	o.radius = num("radius", o.radius);
	o.height = num("height", o.height);
	o.speed = num("speed", o.speed);
	o.angle = num("angle", o.angle);
	o.look_y = num("look_y", o.look_y);
	nhv_camera::set_orbit(o);
	return 0;
}

// core.nhv_bubble_floor({{x=, z=, rx=, rz=, vx=, vz=}...}, floor_y): the
// Plane of Water's bubbles on the floor, drawn by the client (nil or {}:
// none) - nhv_camera.h, client/nhv_bubble_floor.cpp
int ModApiNetHack::l_nhv_bubble_floor(lua_State *L)
{
	std::vector<nhv_camera::Bubble> list;
	if (lua_istable(L, 1)) {
		size_t n = lua_objlen(L, 1);
		for (size_t i = 1; i <= n; i++) {
			lua_rawgeti(L, 1, i);
			if (lua_istable(L, -1)) {
				nhv_camera::Bubble b{};
				const char *keys[] = {"x", "z", "rx", "rz", "vx", "vz"};
				float *vals[] = {&b.x, &b.z, &b.rx, &b.rz, &b.vx, &b.vz};
				for (int k = 0; k < 6; k++) {
					lua_getfield(L, -1, keys[k]);
					*vals[k] = (float)lua_tonumber(L, -1);
					lua_pop(L, 1);
				}
				list.push_back(b);
			}
			lua_pop(L, 1);
		}
	}
	nhv_camera::set_bubbles(list, (float)luaL_optnumber(L, 2, 0));
	return 0;
}

int ModApiNetHack::l_nh_max_glyph(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushinteger(L, nh.nhv_max_glyph());
	return 1;
}

int ModApiNetHack::l_nh_tile_count(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushinteger(L, nh.nhv_tile_count());
	return 1;
}

int ModApiNetHack::l_nh_cmap_name(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushstring(L, nh.nhv_cmap_name(luaL_checkinteger(L, 1)));
	return 1;
}

// nh_cmap_tile(symidx) -> the tile of a map symbol on this level (-1: none)
int ModApiNetHack::l_nh_cmap_tile(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushinteger(L, nh.nhv_cmap_tile(luaL_checkinteger(L, 1)));
	return 1;
}

int ModApiNetHack::l_nh_mon_name(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushstring(L, nh.nhv_mon_name(luaL_checkinteger(L, 1)));
	return 1;
}

int ModApiNetHack::l_nh_obj_name(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushstring(L, nh.nhv_obj_name(luaL_checkinteger(L, 1)));
	return 1;
}

int ModApiNetHack::l_nh_set_freemove(lua_State *L)
{
	if (loaded())
		nh.nhv_set_freemove(lua_toboolean(L, 1));
	return 0;
}

// nh_set_monstates({{id=, in_reach=bool, target=id}, ...})
int ModApiNetHack::l_nh_set_monstates(lua_State *L)
{
	if (!loaded())
		return 0;
	luaL_checktype(L, 1, LUA_TTABLE);
	std::vector<nhv_monstate> st;
	size_t n = lua_objlen(L, 1);
	for (size_t i = 1; i <= n; i++) {
		lua_rawgeti(L, 1, i);
		if (lua_istable(L, -1)) {
			nhv_monstate s{};
			lua_getfield(L, -1, "id");
			s.id = (unsigned)lua_tointeger(L, -1);
			lua_getfield(L, -2, "in_reach");
			s.in_reach = lua_toboolean(L, -1);
			lua_getfield(L, -3, "target");
			s.melee_target = (unsigned)lua_tointeger(L, -1);
			lua_pop(L, 3);
			st.push_back(s);
		}
		lua_pop(L, 1);
	}
	nh.nhv_set_monstates(st.data(), (int)st.size());
	return 0;
}

int ModApiNetHack::l_nh_get_all_monsters(lua_State *L)
{
	REQUIRE_LOADED();
	static std::vector<nhv_monfull> buf(512);
	int n = nh.nhv_get_all_monsters(buf.data(), (int)buf.size());
	if (n > (int)buf.size()) { // (more than that on the level: room for them all)
		buf.resize(n + 64);
		n = nh.nhv_get_all_monsters(buf.data(), (int)buf.size());
	}
	if (n > (int)buf.size())
		n = (int)buf.size();
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		const nhv_monfull &m = buf[i];
		lua_createtable(L, 0, 9);
		set_int(L, "id", m.id);
		set_int(L, "x", m.x);
		set_int(L, "y", m.y);
		set_int(L, "mndx", m.mndx);
		set_int(L, "speed", m.speed);
		set_int(L, "size", m.size);
		set_int(L, "mux", m.mux);
		set_int(L, "muy", m.muy);
		set_int(L, "flags", m.flags);
		set_int(L, "hp", m.hp);
		set_int(L, "hpmax", m.hpmax);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// nh_move_monster(id, x, y) -> 1 moved, 0 refused, -1 gone, -2 busy (try
// again), -3 occupied (the hero or a monster is there)
int ModApiNetHack::l_nh_move_monster(lua_State *L)
{
	if (!loaded()) {
		lua_pushinteger(L, -1);
		return 1;
	}
	lua_pushinteger(L, nh.nhv_move_monster((unsigned)luaL_checkinteger(L, 1),
		luaL_checkinteger(L, 2), luaL_checkinteger(L, 3)));
	return 1;
}

// nh_load() -> true | nil, err: load nethack.dll without starting a game
// (for static data such as monster info)
int ModApiNetHack::l_nh_load(lua_State *L)
{
	std::string err;
	if (!load_library(err)) {
		lua_pushnil(L);
		lua_pushstring(L, err.c_str());
		return 2;
	}
	lua_pushboolean(L, true);
	return 1;
}

int ModApiNetHack::l_nh_num_monsters(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushinteger(L, nh.nhv_num_monsters());
	return 1;
}

// nh_mon_info(mndx) -> {name=, size=, tile=, sym=} or nil
int ModApiNetHack::l_nh_mon_info(lua_State *L)
{
	REQUIRE_LOADED();
	int mndx = luaL_checkinteger(L, 1);
	nhv_moninfo mi;
	if (!nh.nhv_mon_info(mndx, &mi)) {
		lua_pushnil(L);
		return 1;
	}
	lua_createtable(L, 0, 4);
	set_str(L, "name", nh.nhv_mon_name(mndx));
	set_int(L, "size", mi.size);
	set_int(L, "tile", mi.tile);
	char sym[2] = {(char)mi.sym, 0};
	set_str(L, "sym", sym);
	return 1;
}

// nh_get_missiles() -> list of {pid, mon_id, x, y, range, otyp, glyph, tile}
int ModApiNetHack::l_nh_get_missiles(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_missile buf[64];
	int n = nh.nhv_get_missiles(buf, 64);
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		const nhv_missile &m = buf[i];
		lua_createtable(L, 0, 8);
		set_int(L, "pid", m.pid);
		set_int(L, "mon_id", m.mon_id);
		set_int(L, "x", m.x);
		set_int(L, "y", m.y);
		set_int(L, "range", m.range);
		set_int(L, "otyp", m.otyp);
		set_int(L, "glyph", m.glyph);
		set_int(L, "tile", m.tile);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// nh_get_lights() -> list of {x, y, range, at_hero}
int ModApiNetHack::l_nh_get_lights(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_light buf[128];
	int n = nh.nhv_get_lights(buf, 128);
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		const nhv_light &l = buf[i];
		lua_createtable(L, 0, 4);
		set_int(L, "x", l.x);
		set_int(L, "y", l.y);
		set_int(L, "range", l.range);
		lua_pushboolean(L, l.at_hero);
		lua_setfield(L, -2, "at_hero");
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// nh_get_ball() -> nil if not punished, else {bx, by, cx, cy, tile, ball_id, chain_id, carried}:
// the heavy iron ball's square and its chain's
int ModApiNetHack::l_nh_get_ball(lua_State *L)
{
	REQUIRE_LOADED();
	int bx, by, cx, cy, tile;
	unsigned ball_id, chain_id;
	int st = nh.nhv_get_ball(&bx, &by, &cx, &cy, &tile, &ball_id, &chain_id);
	if (!st) {
		lua_pushnil(L);
		return 1;
	}
	lua_createtable(L, 0, 5);
	set_int(L, "bx", bx);
	set_int(L, "by", by);
	set_int(L, "cx", cx);
	set_int(L, "cy", cy);
	set_int(L, "tile", tile);
	set_int(L, "ball_id", ball_id);
	set_int(L, "chain_id", chain_id);
	lua_pushboolean(L, st == 2);
	lua_setfield(L, -2, "carried");
	return 1;
}

// nh_get_rays() -> list of {rid, type, kind, x, y, dx, dy, range, from_hero, caster}
int ModApiNetHack::l_nh_get_rays(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_ray buf[32];
	int n = nh.nhv_get_rays(buf, 32);
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		const nhv_ray &r = buf[i];
		lua_createtable(L, 0, 10);
		set_int(L, "rid", r.rid);
		set_int(L, "type", r.type);
		set_int(L, "kind", r.kind);
		set_int(L, "x", r.x);
		set_int(L, "y", r.y);
		set_int(L, "dx", r.dx);
		set_int(L, "dy", r.dy);
		set_int(L, "range", r.range);
		lua_pushboolean(L, r.from_hero);
		lua_setfield(L, -2, "from_hero");
		set_int(L, "caster", r.caster);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// The player's own files, kept from version to version (each VoxHack
// version is unpacked into a folder of its own): %APPDATA%/NetHackVoxel on
// Windows, else the user path. core.nh_profile_read(name) -> its text or
// nil; core.nh_profile_write(name, text) -> true if written. A name is a
// plain file name (letters, digits, "_", "-", ".").
static std::string profile_path(lua_State *L)
{
	std::string name = luaL_checkstring(L, 1);
	if (name.empty() || name[0] == '.' || name.find_first_not_of(
			"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.") != std::string::npos)
		throw LuaError("bad profile file name: " + name);
	std::string dir;
#ifdef _WIN32
	const char *appdata = getenv("APPDATA");
	if (appdata && *appdata)
		dir = std::string(appdata) + DIR_DELIM "NetHackVoxel";
#endif
	if (dir.empty())
		dir = porting::path_user + DIR_DELIM "nethackvoxel";
	return dir + DIR_DELIM + name;
}

int ModApiNetHack::l_nh_profile_read(lua_State *L)
{
	std::string text;
	if (!fs::ReadFile(profile_path(L), text))
		return 0;
	lua_pushlstring(L, text.data(), text.size());
	return 1;
}

int ModApiNetHack::l_nh_profile_write(lua_State *L)
{
	std::string path = profile_path(L);
	size_t len;
	const char *text = luaL_checklstring(L, 2, &len);
	bool ok = fs::CreateAllDirs(fs::RemoveLastPathComponent(path))
		&& fs::safeWriteToFile(path, std::string_view(text, len));
	if (!ok)
		warningstream << "[nhv] could not write " << path << std::endl;
	lua_pushboolean(L, ok);
	return 1;
}

// core.nh_level_await(minp, maxp, center, radius): the level was just
// rebuilt there; start waiting for it to reach the screen (the part of it
// within radius nodes of center, where you'll be: the rest isn't sent
// until you come nearer). core.nh_level_progress() -> 0..1: how
// much of it has (1: all of it, or not waiting), and 0..1: how much of it
// has arrived, meshed yet or not. (nhv_levelready.h)
int ModApiNetHack::l_nh_level_await(lua_State *L)
{
	v3s16 minp = read_v3s16(L, 1), maxp = read_v3s16(L, 2);
	nhv_levelready::await(minp, maxp, read_v3s16(L, 3), luaL_checkinteger(L, 4));
	// (all of it sent again: blocks the rebuild left as they were too, or
	// they'd never come, already being on the client)
	std::vector<v3s16> blocks;
	v3s16 b0 = getNodeBlockPos(minp), b1 = getNodeBlockPos(maxp);
	for (s16 z = b0.Z; z <= b1.Z; z++)
	for (s16 y = b0.Y; y <= b1.Y; y++)
	for (s16 x = b0.X; x <= b1.X; x++)
		blocks.emplace_back(x, y, z);
	getServer(L)->nhvResendBlocks(blocks);
	return 0;
}

int ModApiNetHack::l_nh_level_progress(lua_State *L)
{
	float received;
	lua_pushnumber(L, nhv_levelready::progress(&received));
	lua_pushnumber(L, received);
	return 2;
}

int ModApiNetHack::l_nh_game_id(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushnumber(L, (double) nh.nhv_game_id());
	return 1;
}

int ModApiNetHack::l_nh_get_spells(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_spell buf[64];
	int n = std::min(nh.nhv_get_spells(buf, 64), 64);
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		lua_createtable(L, 0, 3);
		lua_pushstring(L, buf[i].name);
		lua_setfield(L, -2, "name");
		set_int(L, "tile", buf[i].tile);
		lua_pushboolean(L, buf[i].directional);
		lua_setfield(L, -2, "directional");
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// nh_get_shops() -> list of {door_x, door_y, in_x, in_y, type, keeper}:
// the level's shops (for the signs over their doors)
int ModApiNetHack::l_nh_get_shops(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_shop buf[32];
	int n = std::min(nh.nhv_get_shops(buf, 32), 32);
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		lua_createtable(L, 0, 6);
		set_int(L, "door_x", buf[i].door_x);
		set_int(L, "door_y", buf[i].door_y);
		set_int(L, "in_x", buf[i].in_x);
		set_int(L, "in_y", buf[i].in_y);
		lua_pushstring(L, buf[i].type);
		lua_setfield(L, -2, "type");
		lua_pushstring(L, buf[i].keeper);
		lua_setfield(L, -2, "keeper");
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// nh_get_dungeons() -> list of {name, levels, depth_start} (dnum = index - 1)
int ModApiNetHack::l_nh_get_dungeons(lua_State *L)
{
	REQUIRE_LOADED();
	nhv_dungeon buf[32];
	int n = std::min(nh.nhv_get_dungeons(buf, 32), 32);
	lua_createtable(L, n, 0);
	for (int i = 0; i < n; i++) {
		lua_createtable(L, 0, 3);
		lua_pushstring(L, buf[i].name);
		lua_setfield(L, -2, "name");
		set_int(L, "levels", buf[i].levels);
		set_int(L, "depth_start", buf[i].depth_start);
		set_int(L, "entry", buf[i].entry);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

// nh_level_proto(dnum, dlevel) -> a special level's name, "" if ordinary
int ModApiNetHack::l_nh_level_proto(lua_State *L)
{
	REQUIRE_LOADED();
	lua_pushstring(L, nh.nhv_level_proto(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2)));
	return 1;
}

int ModApiNetHack::l_nh_clear_rays(lua_State *L)
{
	if (loaded())
		nh.nhv_clear_rays();
	return 0;
}

int ModApiNetHack::l_nh_clear_missiles(lua_State *L)
{
	if (loaded())
		nh.nhv_clear_missiles();
	return 0;
}

void ModApiNetHack::Initialize(lua_State *L, int top)
{
	API_FCT(nh_start);
	API_FCT(nh_unload);
	API_FCT(nh_state);
	API_FCT(nh_wait);
	API_FCT(nh_exit_code);
	API_FCT(nh_request);
	API_FCT(nh_send_key);
	API_FCT(nh_send_text);
	API_FCT(nh_send_menu);
	API_FCT(nh_clear_input);
	API_FCT(nh_poll_events);
	API_FCT(nh_get_map);
	API_FCT(nh_get_monsters);
	API_FCT(nh_get_hero);
	API_FCT(nh_get_inventory);
	API_FCT(nh_get_floor_objects);
	API_FCT(nh_jump_squares);
	API_FCT(nh_get_container);
	API_FCT(nh_glyph_info);
	API_FCT(nh_max_glyph);
	API_FCT(nhv_camera);
	API_FCT(nhv_camera_roll);
	API_FCT(nhv_player_drift);
	API_FCT(nhv_bubble_floor);
	API_FCT(nhv_camera_orbit);
	API_FCT(nhv_sam_seconds);
	API_FCT(nhv_map_scroll);
	API_FCT(nhv_hero_light);
	API_FCT(nh_tile_count);
	API_FCT(nh_cmap_name);
	API_FCT(nh_cmap_tile);
	API_FCT(nh_mon_name);
	API_FCT(nh_obj_name);
	API_FCT(nh_get_spells);
	API_FCT(nh_game_id);
	API_FCT(nh_level_await);
	API_FCT(nh_level_progress);
	API_FCT(nh_profile_read);
	API_FCT(nh_profile_write);
	API_FCT(nh_set_freemove);
	API_FCT(nh_set_monstates);
	API_FCT(nh_get_all_monsters);
	API_FCT(nh_get_bubbles);
	API_FCT(nh_move_monster);
	API_FCT(nh_load);
	API_FCT(nh_num_monsters);
	API_FCT(nh_mon_info);
	API_FCT(nh_get_missiles);
	API_FCT(nh_clear_missiles);
	API_FCT(nh_get_lights);
	API_FCT(nh_get_ball);
	API_FCT(nh_get_shops);
	API_FCT(nh_get_dungeons);
	API_FCT(nh_level_proto);
	API_FCT(nh_get_rays);
	API_FCT(nh_clear_rays);
}
