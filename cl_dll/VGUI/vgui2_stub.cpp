// vgui2_stub.cpp - the VGUI2 scoreboard bridge for platforms we do not build
// the panel for.
//
// The PS Vita toolchain has no <dlfcn.h>, which Valve's tier1/interface.h
// pulls in, so vgui2client does not build there. The client still links
// against the CSB_* entry points, so this file provides them and reports the
// panel as unavailable: scoreboard.cpp then keeps drawing the text HUD exactly
// as it did before the VGUI2 work.
//
// Built only when NOT VITA, see cl_dll/CMakeLists.txt.

#include "cs_scoreboard_bridge.h"

extern "C" void CSB_SetEngineFactory( void *factory )
{
	(void)factory;
}

extern "C" int CSB_IsAvailable( void )
{
	return 0;
}

extern "C" void CSB_ShowBoard( const csb_board_t *board )
{
	(void)board;
}

extern "C" void CSB_HideBoard( void )
{
}
