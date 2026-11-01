// VoxHack: Steam (Steamworks), when the game is the Steam build
//
// The Steamworks API (steam_api64.dll) is loaded at runtime, and only when
// it's next to the program - the Steam build ships it; the zip and itch
// builds don't, and play exactly as without (every call here does nothing).
// Started before the window opens (so Steam's overlay can draw over it),
// its callbacks run every frame, shut down at exit.
//
// What the game uses (core.nhv_steam*: l_nethack.cpp): achievements, by
// their API names in Steamworks (the game's own ids: achievements.lua),
// and rich presence (what friends see you doing).

#pragma once

#include <string>

namespace nhv_steam
{
// load and start it (main.cpp, before the window); false: no Steam
bool init();
void shutdown();
// every frame (client/game.cpp)
void run_callbacks();
// running, logged in, the app known
bool active();
// unlock an achievement (and send it at once); false if Steam said no
bool achieve(const std::string &name);
// rich presence: key = value ("" clears that key); clear_presence: all
bool presence(const std::string &key, const std::string &value);
void clear_presence();
}
