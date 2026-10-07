// cs_scoreboard_bridge.h - opaque bridge between the HUD scoreboard code
// (which uses game headers) and the VGUI2 scoreboard panel (which uses
// Valve headers). This header includes neither; both sides stay clean.

#ifndef CS_SCOREBOARD_BRIDGE_H
#define CS_SCOREBOARD_BRIDGE_H

#define CSB_MAX_PLAYERS 32
#define CSB_MAX_TEAMS 4
#define CSB_NAME_LEN 64

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

// Called once by the client when the engine API is ready (may be NULL).
CSB_API void CSB_SetEngineFactory( void *factory );

// True when the VGUI2 scoreboard can be used (engine factory present).
// Cheap; may be called every frame.
CSB_API int CSB_IsAvailable( void );

// Push fresh data and show the panel (lazy-creates everything).
CSB_API void CSB_ShowBoard( const csb_board_t *board );

// Hide the panel.
CSB_API void CSB_HideBoard( void );

#ifdef __cplusplus
}
#endif

#endif // CS_SCOREBOARD_BRIDGE_H
