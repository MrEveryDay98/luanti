// VoxHack: Steam (Steamworks) - see nhv_steam.h

#include "nhv_steam.h"
#include "log.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace nhv_steam
{

#ifdef _WIN32
// (the Steamworks flat API's functions this uses: steam_api_flat.h,
// steam_api.h - SDK 1.65; the interfaces by their versioned accessors)
typedef int (__cdecl *InitFlat_t)(char *err); // ESteamAPIInitResult; err: SteamErrMsg (1024)
typedef void (__cdecl *Void_t)();
typedef void *(__cdecl *Accessor_t)();
typedef bool (__cdecl *SetAchievement_t)(void *stats, const char *name);
typedef bool (__cdecl *StoreStats_t)(void *stats);
typedef bool (__cdecl *SetRichPresence_t)(void *friends, const char *key, const char *value);
typedef void (__cdecl *ClearRichPresence_t)(void *friends);

static HMODULE s_dll = nullptr;
static bool s_active = false;
static Void_t s_run_callbacks = nullptr, s_shutdown = nullptr;
static void *s_stats = nullptr, *s_friends = nullptr;
static SetAchievement_t s_set_achievement = nullptr;
static StoreStats_t s_store_stats = nullptr;
static SetRichPresence_t s_set_presence = nullptr;
static ClearRichPresence_t s_clear_presence = nullptr;

template <typename T>
static T proc(const char *name)
{
	return reinterpret_cast<T>(reinterpret_cast<void *>(GetProcAddress(s_dll, name)));
}

bool init()
{
	if (s_active)
		return true;
	// (only the DLL next to the program: not one found elsewhere on the PATH)
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		return false;
	std::wstring dll(path, n);
	size_t slash = dll.find_last_of(L"\\/");
	dll = (slash == std::wstring::npos ? L"" : dll.substr(0, slash + 1)) + L"steam_api64.dll";
	if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES)
		return false; // (not the Steam build)
	s_dll = LoadLibraryW(dll.c_str());
	if (!s_dll) {
		warningstream << "[nhv] Steam: couldn't load steam_api64.dll" << std::endl;
		return false;
	}
	auto init_flat = proc<InitFlat_t>("SteamAPI_InitFlat");
	s_run_callbacks = proc<Void_t>("SteamAPI_RunCallbacks");
	s_shutdown = proc<Void_t>("SteamAPI_Shutdown");
	auto user_stats = proc<Accessor_t>("SteamAPI_SteamUserStats_v013");
	auto friends = proc<Accessor_t>("SteamAPI_SteamFriends_v018");
	s_set_achievement = proc<SetAchievement_t>("SteamAPI_ISteamUserStats_SetAchievement");
	s_store_stats = proc<StoreStats_t>("SteamAPI_ISteamUserStats_StoreStats");
	s_set_presence = proc<SetRichPresence_t>("SteamAPI_ISteamFriends_SetRichPresence");
	s_clear_presence = proc<ClearRichPresence_t>("SteamAPI_ISteamFriends_ClearRichPresence");
	if (!(init_flat && s_run_callbacks && s_shutdown && user_stats && friends && s_set_achievement
			&& s_store_stats && s_set_presence && s_clear_presence)) {
		warningstream << "[nhv] Steam: steam_api64.dll is missing functions (another SDK version?)"
			<< std::endl;
		FreeLibrary(s_dll);
		s_dll = nullptr;
		return false;
	}
	char err[1024] = {0};
	int r = init_flat(err);
	if (r != 0) {
		// (Steam not running, not logged in, or the app unknown: played
		// without it)
		actionstream << "[nhv] Steam: not started (" << r << "): " << err << std::endl;
		FreeLibrary(s_dll);
		s_dll = nullptr;
		return false;
	}
	s_stats = user_stats();
	s_friends = friends();
	s_active = s_stats && s_friends;
	actionstream << "[nhv] Steam: " << (s_active ? "started" : "started, but without its interfaces")
		<< std::endl;
	return s_active;
}

void shutdown()
{
	if (s_dll) {
		if (s_active)
			s_shutdown();
		FreeLibrary(s_dll);
	}
	s_dll = nullptr;
	s_active = false;
	s_stats = s_friends = nullptr;
}

void run_callbacks()
{
	if (s_active)
		s_run_callbacks();
}

bool active()
{
	return s_active;
}

bool achieve(const std::string &name)
{
	if (!s_active || name.empty())
		return false;
	bool ok = s_set_achievement(s_stats, name.c_str());
	if (ok)
		s_store_stats(s_stats);
	else
		actionstream << "[nhv] Steam: no achievement \"" << name << "\" (not set up in Steamworks?)"
			<< std::endl;
	return ok;
}

bool presence(const std::string &key, const std::string &value)
{
	if (!s_active || key.empty())
		return false;
	return s_set_presence(s_friends, key.c_str(), value.empty() ? nullptr : value.c_str());
}

void clear_presence()
{
	if (s_active)
		s_clear_presence(s_friends);
}

#else // not Windows: no Steam build (yet)

bool init() { return false; }
void shutdown() {}
void run_callbacks() {}
bool active() { return false; }
bool achieve(const std::string &) { return false; }
bool presence(const std::string &, const std::string &) { return false; }
void clear_presence() {}

#endif

}
