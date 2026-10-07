// vgui2_client.cpp - CS client side VGUI2 bootstrap.
//
// The engine hands us its VGui2Factory via CSB_SetEngineFactory (called
// from HUD_MobilityInterface); everything is lazy after that. Nothing
// here includes game headers, so there are no UTL clashes with the
// client's own miniutl copy.

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

// KeyValues system lives in tier2 sources, header is not public.
bool KV_InitKeyValuesSystem( CreateInterfaceFn *factories, int n );

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

extern "C" CSB_API void CSB_SetEngineFactory( void *factory )
{
	if ( !s_engineFactory )
		s_engineFactory = factory;
	fprintf( stderr, "VGUI2-CS: CSB_SetEngineFactory called, factory=%p\n", factory );
}

static void CS_EnsureVGui2()
{
	if ( s_vgui2tried )
		return;
	s_vgui2tried = true;

	if ( !s_engineFactory )
	{
		fprintf( stderr, "VGUI2-CS: FAIL no engine factory\n" );
		return;
	}

	CreateInterfaceFn engineFactory = (CreateInterfaceFn)s_engineFactory;
	CreateInterfaceFn facts[1] = { engineFactory };

	ConnectTier1Libraries( facts, 1 );
	fprintf( stderr, "VGUI2-CS: ConnectTier1 done\n" );
	if ( !KV_InitKeyValuesSystem( facts, 1 ))
	{
		fprintf( stderr, "VGUI2-CS: FAIL KV_InitKeyValuesSystem\n" );
		return;
	}
	fprintf( stderr, "VGUI2-CS: KV_InitKeyValues done\n" );
	ConnectTier2Libraries( facts, 1 );
	fprintf( stderr, "VGUI2-CS: ConnectTier2 done\n" );

	if ( !g_pVGui || !g_pVGuiPanel || !g_pVGuiSurface || !g_pVGuiSchemeManager ||
	     !g_pVGuiInput || !g_pVGuiSystem || !g_pVGuiLocalize )
	{
		fprintf( stderr, "VGUI2-CS: FAIL missing interface g_pVGui=%p g_pVGuiPanel=%p g_pVGuiSurface=%p\n",
			(void*)g_pVGui, (void*)g_pVGuiPanel, (void*)g_pVGuiSurface );
		return;
	}
	fprintf( stderr, "VGUI2-CS: all interfaces present\n" );
	if ( !vgui2::VGui_InitInterfacesList( "CLIENT", facts, 1 ))
	{
		fprintf( stderr, "VGUI2-CS: FAIL VGui_InitInterfacesList\n" );
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
	fprintf( stderr, "VGUI2-CS: READY\n" );
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
