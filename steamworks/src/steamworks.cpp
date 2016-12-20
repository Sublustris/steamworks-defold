// Copyright 2016 Björn Ritzl

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <extension/extension.h>
#include <script/script.h>  // DM_LUA_STACK_CHECK, CheckHashOrString, PushBuffer, Ref, Unref"
#include <dlib/log.h>


#define LIB_NAME "steamworks"
#define MODULE_NAME "steamworks"

#define DLIB_LOG_DOMAIN LIB_NAME

#include "./steam_api.h"

struct SteamworksListener {
  SteamworksListener() {
    m_L = 0;
    m_Callback = LUA_NOREF;
    m_Self = LUA_NOREF;
  }
  lua_State* m_L;
  int        m_Callback;
  int        m_Self;
};

static SteamworksListener steamworksListener;

static void NotifyListener(const char* event) {
  if (steamworksListener.m_Callback == LUA_NOREF) {
    return;
  }

  lua_State* L = steamworksListener.m_L;
  int top = lua_gettop(L);
  lua_rawgeti(L, LUA_REGISTRYINDEX, steamworksListener.m_Callback);

  // Setup self
  lua_rawgeti(L, LUA_REGISTRYINDEX, steamworksListener.m_Self);
  lua_pushvalue(L, -1);
  dmScript::SetInstance(L);

  if (!dmScript::IsInstanceValid(L)) {
    dmLogError("Could not run Steamworks callback because the instance has been deleted.");
    lua_pop(L, 2);
    assert(top == lua_gettop(L));
    return;
  }

  lua_pushstring(L, event);

  int ret = lua_pcall(L, 2, LUA_MULTRET, 0);
  if (ret != 0) {
    dmLogError("Error running Steamworks callback: %s", lua_tostring(L, -1));
    lua_pop(L, 1);
  }
  assert(top == lua_gettop(L));
}

static int SetListener(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  lua_pushvalue(L, 1);
  int cb = dmScript::Ref(L, LUA_REGISTRYINDEX);

  if (steamworksListener.m_Callback != LUA_NOREF) {
    dmScript::Unref(steamworksListener.m_L, LUA_REGISTRYINDEX, steamworksListener.m_Callback);
    dmScript::Unref(steamworksListener.m_L, LUA_REGISTRYINDEX, steamworksListener.m_Self);
  }

  steamworksListener.m_L = dmScript::GetMainThread(L);
  steamworksListener.m_Callback = cb;
  dmScript::GetInstance(L);
  steamworksListener.m_Self = dmScript::Ref(L, LUA_REGISTRYINDEX);
  return 0;
}





class SteamCallbackWrapper {
 public:
    SteamCallbackWrapper();
    STEAM_CALLBACK(SteamCallbackWrapper, OnUserStatsReceived, UserStatsReceived_t, m_CallbackUserStatsReceived);
    STEAM_CALLBACK(SteamCallbackWrapper, OnUserStatsStored, UserStatsStored_t, m_CallbackUserStatsStored);
    STEAM_CALLBACK(SteamCallbackWrapper, OnAchievementStored, UserAchievementStored_t, m_CallbackAchievementStored);
    STEAM_CALLBACK(SteamCallbackWrapper, OnPS3TrophiesInstalled, PS3TrophiesInstalled_t, m_CallbackPS3TrophiesInstalled);
};

SteamCallbackWrapper::SteamCallbackWrapper()
  :
  m_CallbackUserStatsReceived(this, &SteamCallbackWrapper::OnUserStatsReceived),
  m_CallbackUserStatsStored(this, &SteamCallbackWrapper::OnUserStatsStored),
  m_CallbackAchievementStored(this, &SteamCallbackWrapper::OnAchievementStored),
  m_CallbackPS3TrophiesInstalled(this, &SteamCallbackWrapper::OnPS3TrophiesInstalled) {
}

void SteamCallbackWrapper::OnUserStatsReceived(UserStatsReceived_t *pCallback) {
  printf("SteamCallbackWrapper::OnUserStatsReceived\n");
  NotifyListener("OnUserStatsReceived");
}
void SteamCallbackWrapper::OnUserStatsStored(UserStatsStored_t *pCallback) {
  printf("SteamCallbackWrapper::OnUserStatsStored\n");
  NotifyListener("OnUserStatsStored");
}
void SteamCallbackWrapper::OnAchievementStored(UserAchievementStored_t *pCallback) {
  printf("SteamCallbackWrapper::OnAchievementStored\n");
  NotifyListener("OnAchievementStored");
}
void SteamCallbackWrapper::OnPS3TrophiesInstalled(PS3TrophiesInstalled_t *pCallback) {
  printf("SteamCallbackWrapper::OnPS3TrophiesInstalled\n");
  NotifyListener("OnPS3TrophiesInstalled");
}


static ISteamFriends *steamFriends;
static ISteamUser *steamUser;
static ISteamUserStats *steamUserStats;
static SteamCallbackWrapper *steamCallbackWrapper = new SteamCallbackWrapper();

/**
 * Push a CSteamID on the stack
 * The 64 bit representation of the steam id will be
 * converted into a string
 */
static int PushSteamID(lua_State* L, CSteamID steamId) {
  char buf[22];
  snprintf(buf, sizeof(buf), "%llu", steamId.ConvertToUint64());
  lua_pushstring(L, buf);
  return 1;
}

/**
 * Get a CSteamID from the stack
 */
static CSteamID* GetSteamID(lua_State* L, int index) {
  char * pEnd;
  const char * s = lua_tostring(L, index);
  uint64 id = strtoull(s, &pEnd, 10);
  CSteamID *steamId = new CSteamID(id);
  return steamId;
}

static int IsLoggedInUserId(lua_State* L, int index) {
  printf("IsLoggedInUserId %d\n", index);
  if (lua_isnoneornil(L, index)) {
    printf("lua_isnoneornil\n");
    return 1;
  }
  if (lua_gettop(L) == 0) {
    printf("lua_gettop == 0\n");
    return 1;
  }
  if (lua_isstring(L, index)) {
    CSteamID* id = GetSteamID(L, index);
    if (steamUser->GetSteamID().ConvertToUint64() == id->ConvertToUint64()) {
      printf("same id\n");
      return 1;
    }
    printf("different ids\n");
  }
  return 0;
}

/**
 * Print the contents of the stack
 */
static void PrintStack(lua_State* L) {
  int n = lua_gettop(L);
  for (int i = 1; i <= n; i++)  {
    printf("STACK %d %s\r\n", i, lua_tostring(L, i));
  }
}


static int Init(lua_State* L) {
  SteamAPI_Init();
  if (!SteamAPI_IsSteamRunning()) {
    luaL_error(L, "Steam is not running");
  }
  steamFriends = SteamFriends();
  steamUser = SteamUser();
  steamUserStats = SteamUserStats();
  steamUserStats->RequestCurrentStats();
  return 1;
}

static int Update(lua_State* L) {
  SteamAPI_RunCallbacks();
  return 0;
}

static int Final(lua_State* L) {
  SteamAPI_Shutdown();
  return 0;
}

static int GetAchievementInfo(lua_State* L) {
  printf("GetAchievementInfo\n");

  if (steamUserStats == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamUserStats is nil");
    return 2;
  }

  uint32 num = steamUserStats->GetNumAchievements();
  printf("num ach %d\n", num);

  const char* name = steamUserStats->GetAchievementName(0);
  printf("name %s\n", name);

  bool achieved;
  uint32 unlockTime;
  const char* achievementId = lua_tostring(L, lua_gettop(L));
  // steamUserStats->GetAchievement( achievementId, &achieved);
  // lua_pushstring(L, )
  steamUserStats->GetAchievementAndUnlockTime(achievementId, &achieved, &unlockTime);

  int top = lua_gettop(L);

  lua_newtable(L);

  lua_pushboolean(L, achieved);
  lua_setfield(L, -2, "unlocked");

  lua_pushnumber(L, unlockTime);
  lua_setfield(L, -2, "unlockTime");

  // hidden, localizedDescription, localizedName, unlocked, unlockTime

  assert(top + 1 == lua_gettop(L));
  return 1;
}

static int GetAchievementNames(lua_State* L) {
  return 1;
}

static int GetUserInfo(lua_State* L) {
  printf("GetUserInfo\n");

  if (steamFriends == NULL || steamUser == NULL) {
    lua_pushnil(L);
    lua_pushstring(L, "steamFriends or steamUser is nil");
    return 2;
  }

  int top = lua_gettop(L);

  if (IsLoggedInUserId(L, 1)) {
    CSteamID steamId = steamUser->GetSteamID();

    lua_newtable(L);

    PushSteamID(L, steamId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, steamFriends->GetPersonaName());
    lua_setfield(L, -2, "name");

    lua_pushinteger(L, steamUser->GetPlayerSteamLevel());
    lua_setfield(L, -2, "steam_level");
  } else {
    CSteamID *steamId = GetSteamID(L, 1);

    lua_newtable(L);

    PushSteamID(L, *steamId);
    lua_setfield(L, -2, "id");

    lua_pushstring(L, steamFriends->GetFriendPersonaName(*steamId));
    lua_setfield(L, -2, "name");

    lua_pushnumber(L, steamFriends->GetFriendSteamLevel(*steamId));
    lua_setfield(L, -2, "steam_level");

    delete steamId;
  }
  assert(top + 1 == lua_gettop(L));
  return 1;
}

static int GetStatValue(lua_State* L) {
  printf("GetStatValue\n");
  return 1;
}



static const luaL_reg Module_methods[] = {
    { "init", Init },
    { "update", Update },
    { "final", Final },
    { "get_achievement_info", GetAchievementInfo },
    { "get_achievement_names", GetAchievementNames },
    { "get_user_info", GetUserInfo },
    { "get_stat_value", GetStatValue },
    { "set_listener", SetListener },
    {0, 0}
};

static void LuaInit(lua_State* L) {
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);

    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeSteamworks(dmExtension::AppParams* params) {
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeSteamworks(dmExtension::Params* params) {
    LuaInit(params->m_L);
    printf("Registered %s Extension\n", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeSteamworks(dmExtension::AppParams* params) {
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeSteamworks(dmExtension::Params* params) {
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(steamworks, LIB_NAME, AppInitializeSteamworks, AppFinalizeSteamworks, InitializeSteamworks, 0, 0, FinalizeSteamworks)
