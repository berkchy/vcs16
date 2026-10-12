// vgui2_loader.cpp - dlopen()s libvgui2client.so and resolves the CSB_* bridge.
//
// See vgui2_loader.h for why this is a runtime load and not a link dependency.

#include "vgui2_loader.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>




// The load result has to be visible in engine.log, and on Android stderr goes
// to logcat where nobody is looking. The engine console is always there by the
// time this runs: VGUI2_SetEngineFactory() is called from Initialize().
// wrect.h first, for the same reason hud.h orders it that way: cl_dll.h pulls
// in the engine headers that name wrect_t without including it themselves.
#include "wrect.h"
#include "cl_dll.h"

static void VGUI2_Log( const char *fmt, ... )
{
	char message[512];
	va_list args;

	va_start( args, fmt );
	vsnprintf( message, sizeof( message ), fmt, args );
	va_end( args );

	gEngfuncs.Con_Printf( "%s", message );
	fprintf( stderr, "%s", message );
}

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

// Where the library can be, in the order worth trying.
//
// The plain name only finds the library when it is inside the APK, and this
// one is built as a separate target that nothing repacks into the game APK -
// it ships in the patcher's runtime lib directory, which is on no loader's
// search path. The engine's own loader searches the game's filesystem, but
// the client dll has no Sys_LoadLibrary in its engine table to call, so the
// game directory is spelled out here. A library dropped next to cstrike.exe
// is found; so is one the updater puts there.
static void *VGUI2_OpenLibrary( const char *name )
{
	// Ask the engine first. COM_LoadLibrary searches the game's filesystem
	// as well as the process linker path, and pfnGetGameDirectory hands out
	// a relative path ("cstrike") that dlopen resolves against the process
	// directory - which is not the game's parent, so spelling it out here
	// never worked. The engine knows where the game actually is.
	// Ask the engine first, through cdll_int.cpp which is the only place
	// holding the mobility API. COM_LoadLibrary searches the game's
	// filesystem as well as the process linker path, and the game directory
	// this client is told is relative ("cstrike") - dlopen would resolve
	// that against the process directory, which is not the game's parent.
	extern void *VGui2EngineLoadLibrary( void );

	void *handle = VGui2EngineLoadLibrary();
	if( handle )
	{
		VGUI2_Log( "VGUI2: %s loaded by the engine\n", name );
		return handle;
	}
	if( handle )
		return handle;

	handle = VGUI2_DLOPEN( name );
	if( handle )
		return handle;

	const char *gameDir = gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : NULL;

	// Only an absolute path is worth trying by hand. A relative one has
	// already been tried by the loader above, against the wrong root.
	if( !gameDir || gameDir[0] != '/' )
		return NULL;

	char path[512];
	snprintf( path, sizeof( path ), "%s/%s", gameDir, name );

	handle = VGUI2_DLOPEN( path );
	if( handle )
	{
		VGUI2_Log( "VGUI2: loaded %s from the game directory\n", path );
		return handle;
	}

	return NULL;
}

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
	handle = VGUI2_OpenLibrary( VGUI2_LIBNAME );

	if( !handle )
	{
		VGUI2_Log( "VGUI2: %s not present in the APK or in %s, keeping the text scoreboard\n",
			VGUI2_LIBNAME, gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : "(no game dir)" );
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
		VGUI2_Log( "VGUI2: %s does not export the scoreboard bridge, keeping the text scoreboard\n", VGUI2_LIBNAME );
		memset( &bridge, 0, sizeof( bridge ) );
		return NULL;
	}

	// The handle is intentionally leaked: the panel keeps calling into the
	// library until the process exits.
	VGUI2_Log( "VGUI2: scoreboard bridge loaded from %s\n", VGUI2_LIBNAME );
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