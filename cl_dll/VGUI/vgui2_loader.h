// vgui2_loader.h - loads libvgui2client.so at runtime.
//
// The VGUI2 scoreboard lives in its own shared library because Valve's UTL and
// the client's miniutl define the same symbols and cannot share one binary.
// Linking it normally would put it in the client dll's DT_NEEDED, and Android
// refuses to load a library whose dependency is missing: a player on an APK
// that predates the scoreboard could not even start the game. So the client
// dll dlopen()s it instead and every entry point degrades to "unavailable" when
// it is not there, which leaves the text HUD in charge.

#ifndef VGUI2_LOADER_H
#define VGUI2_LOADER_H

#include "cs_scoreboard_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

// The bridge as resolved from libvgui2client.so.
typedef struct vgui2_bridge_s
{
	void	(*pfnSetEngineFactory)( void *factory, void ( *pfnConPrintf )( const char *fmt, ... ) );
	int		(*pfnIsAvailable)( void );
	void	(*pfnShowBoard)( const csb_board_t *board );
	void	(*pfnHideBoard)( void );
	void	(*pfnShowLoading)( const csb_loading_t *info );
	void	(*pfnHideLoading)( void );
} vgui2_bridge_t;

// Loads the library on first use. Returns NULL when it is missing, incomplete,
// or when the platform has no dlopen (PS Vita) - callers treat that as "no
// VGUI2 scoreboard here".
const vgui2_bridge_t *VGUI2_Get( void );

// The same bridge, but never triggers the load. For a per-frame caller that
// only wants to tear state down: going through VGUI2_Get() would pull the
// library in on a client that has no reason to have it yet.
const vgui2_bridge_t *VGUI2_GetIfLoaded( void );

// Hand over the engine's VGUI2 factory. Call once the engine API is ready.
// A NULL factory, or a missing library, simply disables the scoreboard.
void VGUI2_SetEngineFactory( void *factory );

// Print once. The scoreboard asks every frame, and a line per frame is how
// the log used to lose the one line that mattered.
void VGUI2_LogOnce( const char *message );

// Exported for the inline helpers above, which decide what to report and
// cannot reach a static.
void VGUI2_Log( const char *fmt, ... );

static inline int VGUI2_IsAvailable( void )
{
	const vgui2_bridge_t *b = VGUI2_Get();

	if( !b )
	{
		VGUI2_LogOnce( "VGUI2: not available, the text scoreboard is in charge\n" );
		return 0;
	}

	const int available = b->pfnIsAvailable() ? 1 : 0;
	VGUI2_LogOnce( available
		? "VGUI2: the panel library came up\n"
		: "VGUI2: the panel library refused to start, text scoreboard stays\n" );

	return available;
}

static inline void VGUI2_ShowBoard( const csb_board_t *board )
{
	const vgui2_bridge_t *b = VGUI2_Get();

	if( b )
		b->pfnShowBoard( board );
}

static inline void VGUI2_HideBoard( void )
{
	const vgui2_bridge_t *b = VGUI2_Get();

	if( b )
		b->pfnHideBoard();
}

// The loading screen. The engine stops calling pfnRedraw while a level loads,
// so HUD_LoadingPanelDraw is the only place this can come from.
static inline void VGUI2_ShowLoading( const csb_loading_t *info )
{
	const vgui2_bridge_t *b = VGUI2_Get();

	if( b )
		b->pfnShowLoading( info );
}

static inline void VGUI2_HideLoading( void )
{
	const vgui2_bridge_t *b = VGUI2_GetIfLoaded();

	if( b )
		b->pfnHideLoading();
}

#ifdef __cplusplus
}
#endif

#endif // VGUI2_LOADER_H