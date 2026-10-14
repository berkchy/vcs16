// vgui2_client.cpp - CS client side VGUI2 bootstrap.
//
// The engine hands us its VGui2Factory via CSB_SetEngineFactory (called
// from HUD_MobilityInterface); everything is lazy after that. Nothing
// here includes game headers, so there are no UTL clashes with the
// client's own miniutl copy.

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <tier1/interface.h>
#include <tier1/tier1.h>
#include <tier2/tier2.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/IScheme.h>
#include <vgui/IInput.h>
#include <vgui/ISystem.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Controls.h>

#include "cs_scoreboard_bridge.h"

#include "cs_scoreboard_bridge.h" // CSB_API export macro

// The KeyValues system and the interface it hands out live in tier2 sources.
// ConnectTier2Libraries aborts when no factory supplies a KeyValues003, and
// the engine's does not, so the bootstrap supplies one itself.
#include "KeyValuesCompat.h"

// A CreateInterfaceFn of our own, answering only KeyValues003.
//
// ConnectTier2Libraries reads KV_InitKeyValuesSystem returning false as fatal:
// it calls Error(), which aborts, and the game dies in the first frame that
// asks for a scoreboard. The return is not a failure report at all - it says
// no factory supplied a KeyValues003 - and the engine's VGUI2 factory does
// not supply one, so the answer was always false. Passing the built-in
// implementation as a second factory keeps tier2 on the path it expects,
// using the same object the fallback branch would have installed anyway.
//
// Mirrors the engine's VGui2_CreateInterface: a name it does not know comes
// back NULL with a non-zero code, and the `if (!g_pX)` guards in tier2 make
// sure a NULL from here never overwrites what the engine already answered.
static void *CS_KeyValuesFactory( const char *name, int *returnCode )
{
	if ( returnCode )
		*returnCode = 0;

	if ( name && !Q_strcmp( name, IKEYVALUES_INTERFACE_VERSION ) )
		return KV_GetBuiltInKeyValues();

	if ( returnCode )
		*returnCode = 1;

	return NULL;
}

// Extra engine-side interface: load schemes from memory buffers.
// Mirrors engine/client/vgui/vgui2/vgui2_scheme.cpp.
class ICSchemeClientSchemeLoader : public IBaseInterface
{
public:
	virtual vgui2::HScheme LoadSchemeFromBuffer( const char *buffer, const char *tag ) = 0;
};

#define VGUI_SCHEMELOADER_INTERFACE_VERSION "VGUI_SchemeLoader001"

//-----------------------------------------------------------------------------
// Built-in Counter-Strike style scheme.
//-----------------------------------------------------------------------------
static const char s_csSchemeText[] =
"Scheme\n"
"{\n"
"	Colors\n"
"	{\n"
"		\"FgColor\" \"210 210 210 255\"\n"
"		\"BgColor\" \"0 0 0 180\"\n"
"		\"Label.TextColor\" \"255 255 255 255\"\n"
"		\"BrightText\" \"255 220 120 255\"\n"
"		\"DimText\" \"150 150 150 255\"\n"
"		\"CTBlue\" \"140 180 255 255\"\n"
"		\"TOrange\" \"255 150 60 255\"\n"
"		\"HeaderOrange\" \"255 140 0 255\"\n"
"	}\n"
"	BaseSettings\n"
"	{\n"
"		\"FgColor\" \"210 210 210 255\"\n"
"		\"BgColor\" \"0 0 0 180\"\n"
"	}\n"
"	Fonts\n"
"	{\n"
"		\"Default\"\n"
"		{\n"
"			\"1\" { \"name\" \"Arial\" \"tall\" \"15\" \"weight\" \"400\" \"antialias\" \"1\" }\n"
"		}\n"
"		\"Big\"\n"
"		{\n"
"			\"1\" { \"name\" \"Arial\" \"tall\" \"20\" \"weight\" \"700\" \"antialias\" \"1\" }\n"
"		}\n"
"	}\n"
"	Borders\n"
"	{\n"
"	}\n"
"}\n";

static bool s_vgui2ready = false;
static bool s_vgui2tried = false;
static vgui2::HScheme s_csScheme = 0;
static void *s_engineFactory = NULL;

// The engine's Con_Printf, handed over with the factory. Everything this
// bootstrap has to say used to go to stderr, which on Android ends up in
// logcat and nowhere else - so the one file that explains why VGUI2 is not
// coming up printed into a place nobody reads, and the symptom was a score-
// board that stayed text with no reason given anywhere.
static void ( *s_conPrintf )( const char *fmt, ... ) = NULL;

// VGUI2_Log prints through the engine console when we have it, and keeps the
// stderr copy so a logcat capture still shows something.
static void Vgui2Log( const char *fmt, ... )
{
	char message[512];
	va_list args;

	va_start( args, fmt );
	vsnprintf( message, sizeof( message ), fmt, args );
	va_end( args );

	if( s_conPrintf )
		s_conPrintf( "%s", message );
	else
		fprintf( stderr, "%s", message );
}

extern "C" CSB_API void CSB_SetEngineFactory( void *factory, void ( *pfnConPrintf )( const char *fmt, ... ) )
{
	if ( !s_engineFactory )
		s_engineFactory = factory;

	if ( !s_conPrintf )
		s_conPrintf = pfnConPrintf;

	Vgui2Log( "VGUI2-CS: CSB_SetEngineFactory called, factory=%p\n", factory );
}

static void CS_EnsureVGui2()
{
	if ( s_vgui2tried )
		return;
	s_vgui2tried = true;

	if ( !s_engineFactory )
	{
		Vgui2Log( "VGUI2-CS: FAIL no engine factory\n" );
		return;
	}

	CreateInterfaceFn engineFactory = (CreateInterfaceFn)s_engineFactory;

	// The engine first, then ours for the one interface it has no answer for.
	CreateInterfaceFn facts[2] = { engineFactory, CS_KeyValuesFactory };

	ConnectTier1Libraries( facts, 2 );
	Vgui2Log( "VGUI2-CS: ConnectTier1 done\n" );

	// Now expected to be true: CS_KeyValuesFactory answers KeyValues003. The
	// check stays because a false here is the difference between a text
	// scoreboard and an abort, and the line costs one frame.
	if ( !KV_InitKeyValuesSystem( facts, 2 ))
		Vgui2Log( "VGUI2-CS: no engine KeyValues003, using the built-in one\n" );

	ConnectTier2Libraries( facts, 2 );
	Vgui2Log( "VGUI2-CS: ConnectTier2 done\n" );

	if ( !g_pVGui || !g_pVGuiPanel || !g_pVGuiSurface || !g_pVGuiSchemeManager ||
	     !g_pVGuiInput || !g_pVGuiSystem || !g_pVGuiLocalize )
	{
		Vgui2Log( "VGUI2-CS: FAIL missing interface g_pVGui=%p g_pVGuiPanel=%p g_pVGuiSurface=%p "
			"g_pVGuiSchemeManager=%p g_pVGuiInput=%p g_pVGuiSystem=%p g_pVGuiLocalize=%p\n",
			(void*)g_pVGui, (void*)g_pVGuiPanel, (void*)g_pVGuiSurface, (void*)g_pVGuiSchemeManager,
			(void*)g_pVGuiInput, (void*)g_pVGuiSystem, (void*)g_pVGuiLocalize );
		return;
	}
	Vgui2Log( "VGUI2-CS: all interfaces present\n" );
	if ( !vgui2::VGui_InitInterfacesList( "CLIENT", facts, 1 ))
	{
		Vgui2Log( "VGUI2-CS: FAIL VGui_InitInterfacesList\n" );
		return;
	}

	// Client scheme
	int rc = 0;
	ICSchemeClientSchemeLoader *loader = (ICSchemeClientSchemeLoader *)engineFactory( VGUI_SCHEMELOADER_INTERFACE_VERSION, &rc );
	if ( loader && rc == 0 )
		s_csScheme = loader->LoadSchemeFromBuffer( s_csSchemeText, "CSScheme" );
	if ( !s_csScheme )
		s_csScheme = g_pVGuiSchemeManager->GetDefaultScheme();

	g_pVGui->Start();
	s_vgui2ready = true;
	Vgui2Log( "VGUI2-CS: READY\n" );
}

extern "C" int CS_VGui2Available()
{
	CS_EnsureVGui2();
	return s_vgui2ready ? 1 : 0;
}

extern "C" vgui2::HScheme CS_VGui2Scheme()
{
	CS_EnsureVGui2();
	return s_csScheme;
}
