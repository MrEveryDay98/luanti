// NetHack Voxel
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "lua_api/l_base.h"

// Bindings for nethack.dll (see nhcore/nhv_api.h in the NetHack Voxel
// repository). Registered as core.nh_* on the server side.
class ModApiNetHack : public ModApiBase {
private:
	static int l_nh_start(lua_State *L);
	static int l_nh_unload(lua_State *L);
	static int l_nh_state(lua_State *L);
	static int l_nh_wait(lua_State *L);
	static int l_nh_exit_code(lua_State *L);
	static int l_nh_request(lua_State *L);
	static int l_nh_send_key(lua_State *L);
	static int l_nh_send_text(lua_State *L);
	static int l_nh_send_menu(lua_State *L);
	static int l_nh_clear_input(lua_State *L);
	static int l_nh_poll_events(lua_State *L);
	static int l_nh_get_map(lua_State *L);
	static int l_nh_get_monsters(lua_State *L);
	static int l_nh_get_hero(lua_State *L);
	static int l_nh_get_inventory(lua_State *L);
	static int l_nh_get_floor_objects(lua_State *L);
	static int l_nh_get_container(lua_State *L);
	static int l_nh_glyph_info(lua_State *L);
	static int l_nh_max_glyph(lua_State *L);
	static int l_nh_tile_count(lua_State *L);
	static int l_nh_cmap_name(lua_State *L);
	static int l_nh_mon_name(lua_State *L);
	static int l_nh_obj_name(lua_State *L);
	static int l_nh_get_spells(lua_State *L);
	static int l_nh_game_id(lua_State *L);
	static int l_nh_set_freemove(lua_State *L);
	static int l_nh_set_monstates(lua_State *L);
	static int l_nh_get_all_monsters(lua_State *L);
	static int l_nh_move_monster(lua_State *L);
	static int l_nh_load(lua_State *L);
	static int l_nh_num_monsters(lua_State *L);
	static int l_nh_mon_info(lua_State *L);
	static int l_nh_get_missiles(lua_State *L);
	static int l_nh_clear_missiles(lua_State *L);
	static int l_nh_get_rays(lua_State *L);
	static int l_nh_get_lights(lua_State *L);
	static int l_nh_get_ball(lua_State *L);
	static int l_nh_get_shops(lua_State *L);
	static int l_nh_clear_rays(lua_State *L);

public:
	static void Initialize(lua_State *L, int top);
};
