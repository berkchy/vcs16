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
	void	(*pfnSetEngineFactory)( void *factory );
	int		(*pfnIsAvailable)( void );
	void	(*pfnShowBoard)( const csb_board_t *board );
	void	(*pfnHideBoard)( void );
} vgui2_bridge_t;

// Loads the library on first use. Returns NULL when it is missing, incomplete,
// or when the platform has no dlopen (PS Vita) - callers treat that as "no
// VGUI2 scoreboard here".
const vgui2_bridge_t *VGUI2_Get( void );

// Hand over the engine's VGUI2 factory. Call once the engine API is ready.
// A NULL factory, or a missing library, simply disables the scoreboard.
void VGUI2_SetEngineFactory( void *factory );

static inline int VGUI2_IsAvailable( void )
{
	const vgui2_bridge_t *b = VGUI2_Get();
	return ( b && b->pfnIsAvailable() ) ? 1 : 0;
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

#ifdef __cplusplus
}
#endif

#endif // VGUI2_LOADER_H