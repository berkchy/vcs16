// cs_scoreboard_bridge.h - opaque bridge between the HUD scoreboard code
// (which uses game headers) and the VGUI2 scoreboard panel (which uses
// Valve headers). This header includes neither; both sides stay clean.

#ifndef CS_SCOREBOARD_BRIDGE_H
#define CS_SCOREBOARD_BRIDGE_H

#define CSB_MAX_PLAYERS 32
#define CSB_MAX_TEAMS 4
#define CSB_NAME_LEN 64
#define CSB_MAP_LEN 64

// teamnumber values (mirror dlls/cdll_dll.h, repeated here so this header
// stays dependency-free)
#define CSB_TEAM_UNASSIGNED 0
#define CSB_TEAM_TERRORIST 1
#define CSB_TEAM_CT 2
#define CSB_TEAM_SPECTATOR 3

typedef struct
{
	char name[CSB_NAME_LEN];
	int kills;
	int deaths;
	int ping;
	int team;
	int dead;
	int isLocal;
	int isSpectator;
} csb_player_t;

typedef struct
{
	char name[CSB_NAME_LEN];
	int score;
	int players;
	int number;
} csb_team_t;

typedef struct
{
	char serverName[128];
	csb_team_t teams[CSB_MAX_TEAMS];
	int numTeams;
	csb_player_t players[CSB_MAX_PLAYERS];
	int numPlayers;
	int teamplay;
} csb_board_t;

// The loading screen. There is no real progress to report - the engine's
// loadingBar is a texture, not a number - so progress is a 0..100 guess the
// client derives from elapsed load time, or CSB_PROGRESS_UNKNOWN to let the
// panel sweep instead of pretending to know.
#define CSB_PROGRESS_UNKNOWN (-1)

typedef struct
{
	char mapName[CSB_MAP_LEN];
	int progress;
} csb_loading_t;

#ifdef __cplusplus
extern "C" {
#endif

// Exported across the libvgui2client.so boundary (that target builds
// with hidden visibility).
#if defined(_WIN32)
#define CSB_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#define CSB_API __attribute__((visibility("default")))
#else
#define CSB_API
#endif

// Exported by libvgui2client.so. The client dll does NOT link against that
// library - it dlopen()s it and resolves these names at runtime, because a
// DT_NEEDED entry that cannot be satisfied makes the whole client dll fail to
// load. An APK that predates the scoreboard simply has no libvgui2client.so,
// and the client must keep running with the text HUD (see vgui2_loader.h).
// Hand over the engine's VGUI2 CreateInterface, and its Con_Printf.
//
// The Con_Printf comes along because this library has no engine function
// table of its own - that is the whole reason it is a separate .so. Without
// it the only thing its diagnostics could reach was stderr, which on Android
// goes to logcat, so every line this bootstrap printed was invisible in
// engine.log. Passed as a plain variadic function rather than the engine's
// APIProxy type so this header stays dependency-free.
CSB_API void CSB_SetEngineFactory( void *factory, void ( *pfnConPrintf )( const char *fmt, ... ) );

// True when the VGUI2 scoreboard can be used (library loaded and the engine
// handed us a factory). Cheap; may be called every frame.
CSB_API int CSB_IsAvailable( void );

// Push fresh data and show the panel (lazy-creates everything).
CSB_API void CSB_ShowBoard( const csb_board_t *board );

// Hide the panel.
CSB_API void CSB_HideBoard( void );

// The loading screen. Only ever reached through HUD_LoadingPanelDraw, which
// the engine calls once per frame while a level loads - it does not call
// pfnRedraw then, so there is no other moment the panel could be drawn from.
CSB_API void CSB_ShowLoading( const csb_loading_t *info );

// Take the loading screen down. Called when the load ends, and from
// shutdown, so it has to survive a client that never brought VGUI2 up.
CSB_API void CSB_HideLoading( void );

#ifdef __cplusplus
}
#endif

#endif // CS_SCOREBOARD_BRIDGE_H
