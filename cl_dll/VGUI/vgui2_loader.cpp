// vgui2_loader.cpp - dlopen()s libvgui2client.so and resolves the CSB_* bridge.
//
// See vgui2_loader.h for why this is a runtime load and not a link dependency.

#include "vgui2_loader.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define VGUI2_DLOPEN( name )	((void *)LoadLibraryA( name ))
#define VGUI2_DLSYM( h, n )		((void *)GetProcAddress( (HMODULE)(h), n ))
#define VGUI2_LIBNAME			"vgui2client.dll"
#elif defined( __APPLE__ )
#include <dlfcn.h>
#define VGUI2_DLOPEN( name )	dlopen( name, RTLD_LAZY | RTLD_LOCAL )
#define VGUI2_DLSYM( h, n )		dlsym( (h), n )
#define VGUI2_LIBNAME			"libvgui2client.dylib"
#elif defined( __linux__ ) || defined( __ANDROID__ )
#include <dlfcn.h>
#define VGUI2_DLOPEN( name )	dlopen( name, RTLD_LAZY | RTLD_LOCAL )
#define VGUI2_DLSYM( h, n )		dlsym( (h), n )
#if defined( __ANDROID__ )
#define VGUI2_LIBNAME			"libvgui2client.so"
#else
#define VGUI2_LIBNAME			"./libvgui2client.so"
#endif
#else
// No dynamic loader on this platform (PS Vita). The scoreboard stays on the
// text HUD, which is what the stub used to do.
#define VGUI2_LIBNAME			NULL
#endif

static vgui2_bridge_t bridge;
static int tried;

const vgui2_bridge_t *VGUI2_Get( void )
{
	void *handle;

	if( tried )
		return bridge.pfnShowBoard ? &bridge : NULL;

	// Set before anything can fail: one miss is enough, we do not retry per frame.
	tried = 1;

#ifdef VGUI2_LIBNAME
	handle = VGUI2_DLOPEN( VGUI2_LIBNAME );

	if( !handle )
	{
		fprintf( stderr, "VGUI2: %s not present, keeping the text scoreboard\n", VGUI2_LIBNAME );
		return NULL;
	}

	bridge.pfnSetEngineFactory = (void (*)( void * ))VGUI2_DLSYM( handle, "CSB_SetEngineFactory" );
	bridge.pfnIsAvailable = (int (*)( void ))VGUI2_DLSYM( handle, "CSB_IsAvailable" );
	bridge.pfnShowBoard = (void (*)( const csb_board_t * ))VGUI2_DLSYM( handle, "CSB_ShowBoard" );
	bridge.pfnHideBoard = (void (*)( void ))VGUI2_DLSYM( handle, "CSB_HideBoard" );

	// Every name is required: a half-resolved bridge would crash on the call
	// that happens to be non-NULL.
	if( !bridge.pfnSetEngineFactory || !bridge.pfnIsAvailable ||
		!bridge.pfnShowBoard || !bridge.pfnHideBoard )
	{
		fprintf( stderr, "VGUI2: %s does not export the scoreboard bridge, keeping the text scoreboard\n", VGUI2_LIBNAME );
		memset( &bridge, 0, sizeof( bridge ) );
		return NULL;
	}

	// The handle is intentionally leaked: the panel keeps calling into the
	// library until the process exits.
	fprintf( stderr, "VGUI2: scoreboard bridge loaded from %s\n", VGUI2_LIBNAME );
	return &bridge;
#else
	return NULL;
#endif
}

extern "C" void VGUI2_SetEngineFactory( void *factory )
{
	const vgui2_bridge_t *b = VGUI2_Get();

	if( b )
		b->pfnSetEngineFactory( factory );
}