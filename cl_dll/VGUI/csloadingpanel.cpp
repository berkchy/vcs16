// csloadingpanel.cpp - the VGUI2 loading screen.
//
// The engine owns the loading bar and stops calling pfnRedraw while a level
// loads, so this panel is only ever painted from HUD_LoadingPanelDraw. This
// TU includes Valve headers only; the load state arrives through the bridge.

#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>

#include <tier1/tier1.h>
#include <tier2/tier2.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/Label.h>
#include <Color.h>

#include "cs_scoreboard_bridge.h"
#include "csloadingpanel.h"

extern "C" int CS_VGui2Available();
extern "C" vgui2::HScheme CS_VGui2Scheme();

namespace
{

const int BAR_W = 420;
const int BAR_H = 14;

// How long one end-to-end sweep takes. Short enough to read as motion, long
// enough not to strobe on a phone at the 1-in-4 paint rate.
const double SWEEP_SECONDS = 1.6;

// Progress means the same thing on every load, so "the number changed" is not
// a way to spot a new one - every load of an unknown-progress map reports -1.
// The idle state is a value no load can carry instead.
const int IDLE_PROGRESS = -2;

::Color BarBack()   { return ::Color( 40, 40, 40, 255 ); }
::Color BarEdge()   { return ::Color( 90, 90, 90, 255 ); }
::Color BarFill()   { return ::Color( 255, 140, 0, 255 ); }
::Color MapOrange() { return ::Color( 255, 140, 0, 255 ); }
::Color SubGray()   { return ::Color( 160, 160, 160, 255 ); }

double Now()
{
	struct timespec ts;

	if( clock_gettime( CLOCK_MONOTONIC, &ts ) != 0 )
		return 0.0;

	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

} // namespace

CSLoadingPanel *CSLoadingPanel::s_instance = NULL;

CSLoadingPanel::CSLoadingPanel()
	: BaseClass( NULL, "CSLoadingPanel", false )
	, m_startTime( 0.0 )
	, m_lastProgress( IDLE_PROGRESS )
	, m_unknown( true )
{
	SetTitleBarVisible( false );
	SetMoveable( false );
	SetSizeable( false );
	SetProportional( false );

	// Opaque, because the point is to stand in for the whole screen while the
	// console and the menu keep painting underneath.
	SetBgColor( ::Color( 0, 0, 0, 255 ) );
	SetPaintBackgroundEnabled( true );

	vgui2::HScheme scheme = CS_VGui2Scheme();
	if( scheme )
		SetScheme( scheme );

	m_pMap = new vgui2::Label( this, "LoadMap", "" );
	m_pMap->SetContentAlignment( vgui2::Label::a_center );
	m_pMap->SetFgColor( MapOrange() );

	m_pHint = new vgui2::Label( this, "LoadHint", "Loading" );
	m_pHint->SetContentAlignment( vgui2::Label::a_center );
	m_pHint->SetFgColor( SubGray() );
}

CSLoadingPanel::~CSLoadingPanel()
{
	if ( s_instance == this )
		s_instance = NULL;
}

CSLoadingPanel *CSLoadingPanel::Get()
{
	if ( !s_instance )
	{
		s_instance = new CSLoadingPanel();
		vgui2::VPANEL vp = s_instance->GetVPanel();
		vgui2::VPANEL embedded = vgui2::surface()->GetEmbeddedPanel();
		if ( embedded )
			vgui2::ipanel()->SetParent( vp, embedded );
		s_instance->SetVisible( false );
	}

	return s_instance;
}

void CSLoadingPanel::Shutdown()
{
	if ( s_instance )
	{
		delete s_instance;
		s_instance = NULL;
	}
}

void CSLoadingPanel::SetLoading( const csb_loading_t *info, double now )
{
	if ( !info )
		return;

	int sw = 1280, sh = 720;
	vgui2::surface()->GetScreenSize( sw, sh );

	// Full screen, so the classic bar underneath it is never seen.
	SetPos( 0, 0 );
	SetSize( sw, sh );

	vgui2::IScheme *is = vgui2::scheme()->GetIScheme( GetScheme() );
	vgui2::HFont bigFont = is ? is->GetFont( "Big", false ) : vgui2::INVALID_FONT;

	const int mapY = sh / 2 - BAR_H / 2 - 34;

	m_pMap->SetText( info->mapName[0] ? info->mapName : "" );
	m_pMap->SetFont( bigFont );
	m_pMap->SetPos( 0, mapY );
	m_pMap->SetSize( sw, 24 );
	m_pMap->SetVisible( info->mapName[0] != '\0' );

	m_pHint->SetFont( bigFont );
	m_pHint->SetPos( 0, mapY + BAR_H + 34 );
	m_pHint->SetSize( sw, 20 );

	// A new load restarts the sweep from the left. Without this a reload picks
	// up wherever the previous one was, which looks like a stall. The idle
	// marker is what makes this fire on the second load as well.
	if ( info->progress != m_lastProgress )
		m_startTime = now;

	m_lastProgress = info->progress;
	m_unknown = ( info->progress < 0 );

	if ( m_unknown )
		m_pHint->SetText( "Loading" );
	else
	{
		char hint[32];
		snprintf( hint, sizeof( hint ), "Loading... %d%%", info->progress );
		m_pHint->SetText( hint );
	}

	SetVisible( true );
	MoveToFront();

	// The sweep moves on its own, so a repaint has to be asked for every
	// frame or the bar freezes mid-travel while the load continues.
	InvalidateLayout( false );
	vgui2::ipanel()->Repaint( GetVPanel() );
}

void CSLoadingPanel::SetLoadingDone()
{
	if ( !s_instance )
		return;

	s_instance->SetVisible( false );

	// Armed so the next load reads as a change even when it reports the same
	// progress as this one did.
	s_instance->m_lastProgress = IDLE_PROGRESS;
}

void CSLoadingPanel::Paint()
{
	BaseClass::Paint();

	int sw, sh;
	vgui2::surface()->GetScreenSize( sw, sh );

	const int barX = ( sw - BAR_W ) / 2;
	const int barY = sh / 2 - BAR_H / 2;

	vgui2::surface()->DrawSetColor( BarEdge() );
	vgui2::surface()->DrawOutlinedRect( barX, barY, barX + BAR_W, barY + BAR_H );

	vgui2::surface()->DrawSetColor( BarBack() );
	vgui2::surface()->DrawFilledRect( barX + 1, barY + 1, barX + BAR_W - 1, barY + BAR_H - 1 );

	int fill;

	if ( m_unknown )
	{
		// Triangle wave: out to full and back, so the bar never looks stuck
		// at either end.
		double t = fmod( ( Now() - m_startTime ) / SWEEP_SECONDS, 2.0 );

		if ( t > 1.0 )
			t = 2.0 - t;

		fill = (int)( ( BAR_W - 2 ) * t );
	}
	else
	{
		int pct = m_lastProgress;

		if ( pct < 0 )
			pct = 0;
		if ( pct > 100 )
			pct = 100;

		fill = (int)( ( BAR_W - 2 ) * pct / 100 );
	}

	if ( fill > 0 )
	{
		vgui2::surface()->DrawSetColor( BarFill() );
		vgui2::surface()->DrawFilledRect( barX + 1, barY + 1, barX + 1 + fill, barY + BAR_H - 1 );
	}
}

//-----------------------------------------------------------------------------
// Bridge functions.
//-----------------------------------------------------------------------------
extern "C" CSB_API void CSB_ShowLoading( const csb_loading_t *info )
{
	// Same guard as the scoreboard: this is reached from the engine's loading
	// path, which runs on clients that never brought VGUI2 up at all, and
	// Get() would touch a VGUI2 that does not exist there.
	if ( !info || !CS_VGui2Available() )
		return;

	CSLoadingPanel::Get()->SetLoading( info, Now() );
}

extern "C" CSB_API void CSB_HideLoading( void )
{
	CSLoadingPanel::SetLoadingDone();
}