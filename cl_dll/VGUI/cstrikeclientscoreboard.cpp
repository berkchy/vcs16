// cstrikeclientscoreboard.cpp - Steam-style Counter-Strike scoreboard.
// VGUI2 implementation fed with plain snapshots (see cs_scoreboard_bridge.h).
// This TU includes Valve headers only; game data arrives via the bridge.

#include <cstdio>
#include <cstring>

#include <tier1/tier1.h>
#include <tier2/tier2.h>
#include <vgui/ISurface.h>
#include <vgui/IVGui.h>
#include <vgui/IPanel.h>
#include <vgui/IScheme.h>
#include <vgui_controls/Controls.h>
#include <Color.h>

#include <vgui_controls/Controls.h>
#include <Color.h>

#include "cs_scoreboard_bridge.h"
#include "cstrikeclientscoreboard.h"

extern "C" int CS_VGui2Available();
extern "C" vgui2::HScheme CS_VGui2Scheme();

namespace
{

const int SB_WIDTH = 620;
const int SB_ROW_H = 17;
const int SB_MARGIN = 16;

const int COL_NAME_X = 16;
const int COL_NAME_W = 330;
const int COL_KILLS_X = 372;
const int COL_KILLS_W = 60;
const int COL_DEATHS_X = 442;
const int COL_DEATHS_W = 60;
const int COL_PING_X = 512;
const int COL_PING_W = 76;

::Color White() { return ::Color( 255, 255, 255, 255 ); }
::Color Gray() { return ::Color( 150, 150, 150, 255 ); }
::Color Orange() { return ::Color( 255, 140, 0, 255 ); }
::Color CTBlue() { return ::Color( 140, 180, 255, 255 ); }
::Color TOrange() { return ::Color( 255, 150, 60, 255 ); }

const char *TeamDisplayName( int number, const char *name )
{
	if ( number == CSB_TEAM_CT )
		return "Counter-Terrorists";
	if ( number == CSB_TEAM_TERRORIST )
		return "Terrorists";
	return ( name && name[0] ) ? name : "Players";
}

::Color TeamColor( int number )
{
	if ( number == CSB_TEAM_CT )
		return CTBlue();
	if ( number == CSB_TEAM_TERRORIST )
		return TOrange();
	return White();
}

} // namespace

CCStrikeClientScoreBoard *CCStrikeClientScoreBoard::s_instance = NULL;

CCStrikeClientScoreBoard::CCStrikeClientScoreBoard()
	: BaseClass( NULL, "CStrikeScoreBoard", false )
	, m_rowCount( 0 )
{
	SetTitleBarVisible( false );
	SetTitleBarVisible( false );
	SetMoveable( false );
	SetSizeable( false );
	SetProportional( false );
	SetBgColor( ::Color( 0, 0, 0, 178 ) );
	SetPaintBackgroundEnabled( true );

	vgui2::HScheme scheme = CS_VGui2Scheme();
	if ( scheme )
		SetScheme( scheme );

	m_pTitle = new vgui2::Label( this, "SBTitle", "" );
	m_pTitle->SetContentAlignment( vgui2::Label::a_center );
	m_pTitle->SetFgColor( Orange() );

	m_pColName = new vgui2::Label( this, "SBColName", "Player" );
	m_pColKills = new vgui2::Label( this, "SBColKills", "Kills" );
	m_pColDeaths = new vgui2::Label( this, "SBColDeaths", "Deaths" );
	m_pColPing = new vgui2::Label( this, "SBColPing", "Ping" );
	m_pColName->SetFgColor( Orange() );
	m_pColKills->SetFgColor( Orange() );
	m_pColDeaths->SetFgColor( Orange() );
	m_pColPing->SetFgColor( Orange() );
	m_pColKills->SetContentAlignment( vgui2::Label::a_east );
	m_pColDeaths->SetContentAlignment( vgui2::Label::a_east );
	m_pColPing->SetContentAlignment( vgui2::Label::a_east );

	m_pTeamA = new vgui2::Label( this, "SBTeamA", "" );
	m_pTeamB = new vgui2::Label( this, "SBTeamB", "" );
	m_pSpectators = new vgui2::Label( this, "SBSpectators", "" );
	m_pSpectators->SetFgColor( Gray() );

	for ( int i = 0; i < CSB_MAX_PLAYERS; i++ )
	{
		m_rows[i].name = NULL;
		m_rows[i].kills = NULL;
		m_rows[i].deaths = NULL;
		m_rows[i].ping = NULL;
	}
}

CCStrikeClientScoreBoard::~CCStrikeClientScoreBoard()
{
	if ( s_instance == this )
		s_instance = NULL;
}

CCStrikeClientScoreBoard *CCStrikeClientScoreBoard::Get()
{
	if ( !s_instance )
	{
		s_instance = new CCStrikeClientScoreBoard();
		vgui2::VPANEL vp = s_instance->GetVPanel();
		vgui2::VPANEL embedded = vgui2::surface()->GetEmbeddedPanel();
		if ( embedded )
			vgui2::ipanel()->SetParent( vp, embedded );
		s_instance->SetVisible( false );
	}
	return s_instance;
}

void CCStrikeClientScoreBoard::Shutdown()
{
	if ( s_instance )
	{
		delete s_instance;
		s_instance = NULL;
	}
}

void CCStrikeClientScoreBoard::EnsureRows( int count )
{
	if ( count > CSB_MAX_PLAYERS )
		count = CSB_MAX_PLAYERS;
	vgui2::HScheme scheme = GetScheme();
	while ( m_rowCount < count )
	{
		char name[32];
		snprintf( name, sizeof( name ), "SBRow%d", m_rowCount );
		Row &r = m_rows[m_rowCount];
		r.name = new vgui2::Label( this, name, "" );
		r.kills = new vgui2::Label( this, name, "" );
		r.deaths = new vgui2::Label( this, name, "" );
		r.ping = new vgui2::Label( this, name, "" );
		if ( scheme )
		{
			r.name->SetScheme( scheme );
			r.kills->SetScheme( scheme );
			r.deaths->SetScheme( scheme );
			r.ping->SetScheme( scheme );
		}
		r.kills->SetContentAlignment( vgui2::Label::a_east );
		r.deaths->SetContentAlignment( vgui2::Label::a_east );
		r.ping->SetContentAlignment( vgui2::Label::a_east );
		m_rowCount++;
	}
}

void CCStrikeClientScoreBoard::SetBoard( const csb_board_t *board )
{
	if ( !board )
		return;

	// Screen-clamped width.
	int sw = 1280, sh = 720;
	vgui2::surface()->GetScreenSize( sw, sh );
	int wide = SB_WIDTH;
	if ( wide > sw - 20 )
		wide = sw - 20;
	if ( wide < 320 )
		wide = 320;

	vgui2::IScheme *scheme = vgui2::scheme()->GetIScheme( GetScheme() );
	vgui2::HFont bigFont = scheme ? scheme->GetFont( "Big", false ) : vgui2::INVALID_FONT;
	vgui2::HFont defFont = scheme ? scheme->GetFont( "Default", false ) : vgui2::INVALID_FONT;
	m_pTitle->SetFont( bigFont );
	m_pTeamA->SetFont( bigFont );
	m_pTeamB->SetFont( bigFont );

	int y = 8;

	// Title: server name.
	char title[160];
	if ( board->serverName[0] )
		snprintf( title, sizeof( title ), "%s", board->serverName );
	else
		snprintf( title, sizeof( title ), "Counter-Strike" );
	m_pTitle->SetText( title );
	m_pTitle->SetPos( SB_MARGIN, y );
	m_pTitle->SetSize( wide - SB_MARGIN * 2, 24 );
	m_pTitle->SetVisible( true );
	y += 28;

	// Column headers.
	m_pColName->SetText( "Player" );
	m_pColName->SetPos( COL_NAME_X, y );
	m_pColName->SetSize( COL_NAME_W, 16 );
	m_pColKills->SetPos( COL_KILLS_X, y );
	m_pColKills->SetSize( COL_KILLS_W, 16 );
	m_pColDeaths->SetPos( COL_DEATHS_X, y );
	m_pColDeaths->SetSize( COL_DEATHS_W, 16 );
	m_pColPing->SetPos( COL_PING_X, y );
	m_pColPing->SetSize( COL_PING_W, 16 );
	m_pColName->SetVisible( true );
	m_pColKills->SetVisible( true );
	m_pColDeaths->SetVisible( true );
	m_pColPing->SetVisible( true );
	if ( defFont != vgui2::INVALID_FONT )
	{
		m_pColName->SetFont( defFont );
		m_pColKills->SetFont( defFont );
		m_pColDeaths->SetFont( defFont );
		m_pColPing->SetFont( defFont );
	}
	y += 20;

	// Order players: CT section, T section, others, spectators last.
	// Within a section: frags desc, deaths asc.
	int n = 0;
	if ( board->numPlayers > CSB_MAX_PLAYERS )
		n = CSB_MAX_PLAYERS;
	else
		n = board->numPlayers;

	int seq[CSB_MAX_PLAYERS];
	for ( int i = 0; i < n; i++ )
		seq[i] = i;
	// simple insertion sort over (section, frags desc, deaths asc)
	for ( int i = 1; i < n; i++ )
	{
		int j = i;
		while ( j > 0 )
		{
			const csb_player_t &a = board->players[seq[j - 1]];
			const csb_player_t &b = board->players[seq[j]];
			int sa = ( a.isSpectator || a.team == CSB_TEAM_SPECTATOR || a.team == CSB_TEAM_UNASSIGNED ) ? 3 : ( a.team == CSB_TEAM_CT ? 0 : ( a.team == CSB_TEAM_TERRORIST ? 1 : 2 ));
			int sb = ( b.isSpectator || b.team == CSB_TEAM_SPECTATOR || b.team == CSB_TEAM_UNASSIGNED ) ? 3 : ( b.team == CSB_TEAM_CT ? 0 : ( b.team == CSB_TEAM_TERRORIST ? 1 : 2 ));
			bool swap = false;
			if ( sa != sb )
				swap = sa > sb;
			else if ( sa < 3 && a.kills != b.kills )
				swap = a.kills < b.kills;
			else if ( sa < 3 )
				swap = a.deaths > b.deaths;
			if ( !swap )
				break;
			int t = seq[j - 1];
			seq[j - 1] = seq[j];
			seq[j] = t;
			j--;
		}
	}

	EnsureRows( n );

	int row = 0;
	int lastSection = -1;
	vgui2::Label *teamLabels[2] = { m_pTeamA, m_pTeamB };
	int teamLabelUsed = 0;
	char specBuf[512];
	specBuf[0] = 0;
	int specLen = 0;

	for ( int i = 0; i < n; i++ )
	{
		const csb_player_t &pl = board->players[seq[i]];
		int section = ( pl.isSpectator || pl.team == CSB_TEAM_SPECTATOR || pl.team == CSB_TEAM_UNASSIGNED ) ? 3 : ( pl.team == CSB_TEAM_CT ? 0 : ( pl.team == CSB_TEAM_TERRORIST ? 1 : 2 ));

		if ( section == 3 )
		{
			// collect spectator names, comma separated
			if ( specLen > 0 && specLen < (int)sizeof( specBuf ) - 2 )
			{
				specBuf[specLen++] = ',';
				specBuf[specLen++] = ' ';
			}
			int k = 0;
			while ( pl.name[k] && specLen < (int)sizeof( specBuf ) - 1 )
				specBuf[specLen++] = pl.name[k++];
			specBuf[specLen] = 0;
			continue;
		}

		if ( section != lastSection )
		{
			lastSection = section;
			if ( teamLabelUsed < 2 )
			{
				// find team score for this section
				int score = 0;
				const char *tname = "";
				for ( int t = 0; t < board->numTeams; t++ )
				{
					int ts = ( board->teams[t].number == CSB_TEAM_CT ) ? 0 : ( board->teams[t].number == CSB_TEAM_TERRORIST ? 1 : 2 );
					if ( ts == section )
					{
						score = board->teams[t].score;
						tname = board->teams[t].name;
						break;
					}
				}
				char hdr[128];
				snprintf( hdr, sizeof( hdr ), "%s: %d", TeamDisplayName( section == 0 ? CSB_TEAM_CT : ( section == 1 ? CSB_TEAM_TERRORIST : -1 ), tname ), score );
				teamLabels[teamLabelUsed]->SetText( hdr );
				teamLabels[teamLabelUsed]->SetFgColor( TeamColor( section == 0 ? CSB_TEAM_CT : CSB_TEAM_TERRORIST ));
				teamLabels[teamLabelUsed]->SetPos( COL_NAME_X, y );
				teamLabels[teamLabelUsed]->SetSize( wide - COL_NAME_X - SB_MARGIN, 22 );
				teamLabels[teamLabelUsed]->SetVisible( true );
				teamLabelUsed++;
				y += 24;
			}
		}

		Row &r = m_rows[row++];
		r.name->SetText( pl.name );
		r.name->SetFgColor( pl.dead ? Gray() : ( pl.isLocal ? Orange() : White() ));
		char num[32];
		snprintf( num, sizeof( num ), "%d", pl.kills );
		r.kills->SetText( num );
		r.kills->SetFgColor( pl.dead ? Gray() : White() );
		snprintf( num, sizeof( num ), "%d", pl.deaths );
		r.deaths->SetText( num );
		r.deaths->SetFgColor( pl.dead ? Gray() : White() );
		snprintf( num, sizeof( num ), "%d", pl.ping );
		r.ping->SetText( num );
		r.ping->SetFgColor( pl.dead ? Gray() : White() );
		if ( defFont != vgui2::INVALID_FONT )
		{
			r.name->SetFont( defFont );
			r.kills->SetFont( defFont );
			r.deaths->SetFont( defFont );
			r.ping->SetFont( defFont );
		}
		r.name->SetPos( COL_NAME_X, y );
		r.name->SetSize( COL_NAME_W, SB_ROW_H );
		r.kills->SetPos( COL_KILLS_X, y );
		r.kills->SetSize( COL_KILLS_W, SB_ROW_H );
		r.deaths->SetPos( COL_DEATHS_X, y );
		r.deaths->SetSize( COL_DEATHS_W, SB_ROW_H );
		r.ping->SetPos( COL_PING_X, y );
		r.ping->SetSize( COL_PING_W, SB_ROW_H );
		r.name->SetVisible( true );
		r.kills->SetVisible( true );
		r.deaths->SetVisible( true );
		r.ping->SetVisible( true );
		y += SB_ROW_H;
	}

	// hide unused rows and team labels
	for ( int i = row; i < m_rowCount; i++ )
	{
		m_rows[i].name->SetVisible( false );
		m_rows[i].kills->SetVisible( false );
		m_rows[i].deaths->SetVisible( false );
		m_rows[i].ping->SetVisible( false );
	}
	for ( int i = teamLabelUsed; i < 2; i++ )
		teamLabels[i]->SetVisible( false );

	// spectators footer
	if ( specLen > 0 )
	{
		char footer[600];
		snprintf( footer, sizeof( footer ), "Spectators: %s", specBuf );
		m_pSpectators->SetText( footer );
		m_pSpectators->SetPos( COL_NAME_X, y + 4 );
		m_pSpectators->SetSize( wide - COL_NAME_X - SB_MARGIN, SB_ROW_H );
		m_pSpectators->SetVisible( true );
		if ( defFont != vgui2::INVALID_FONT )
			m_pSpectators->SetFont( defFont );
		y += SB_ROW_H + 8;
	}
	else m_pSpectators->SetVisible( false );

	SetSize( wide, y + 8 );
	MoveToCenterOfScreen();
}

//-----------------------------------------------------------------------------
// Bridge functions.
//-----------------------------------------------------------------------------
extern "C" CSB_API int CSB_IsAvailable()
{
	extern int CS_VGui2Available();
	return CS_VGui2Available();
}

extern "C" CSB_API void CSB_ShowBoard( const csb_board_t *board )
{
	if ( !board || !CSB_IsAvailable() )
		return;
	CCStrikeClientScoreBoard *sb = CCStrikeClientScoreBoard::Get();
	sb->SetBoard( board );
	vgui2::VPANEL vp = sb->GetVPanel();
	vgui2::VPANEL embedded = vgui2::surface()->GetEmbeddedPanel();
	if ( embedded )
		vgui2::ipanel()->SetParent( vp, embedded );
	sb->SetVisible( true );
	sb->MoveToFront();
}

extern "C" CSB_API void CSB_HideBoard()
{
	if ( CCStrikeClientScoreBoard::Get() )
		CCStrikeClientScoreBoard::Get()->SetVisible( false );
}
