/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "Framework.h"
#include "Bitmap.h"
#include "YesNoMessageBox.h"
#include "Table.h"
#include "keydefs.h"
#include "Switch.h"
#include "Field.h"
#include "utlvector.h"

#define ART_BANNER_INET		"gfx/shell/head_inetgames"
#define ART_BANNER_LAN		"gfx/shell/head_lan"
#define ART_BANNER_LOCK		"gfx/shell/lock"
#define ART_BTN_FAVORITE	"gfx/shell/btn_favorite"
#define ART_BTN_UNFAVORITE	"gfx/shell/btn_unfavorite"

// row and cell tints, derived from the WON gold scheme so the browser keeps
// the palette of the rest of the menu
#define ROW_COLOR_GOLDSRC	0x30203038
#define ROW_COLOR_FAVORITE	0x30382810
#define COLOR_BADGE_GOLDSRC	0xFF6EA9D8
#define COLOR_PING_GOOD		0xFF6CC06C
#define COLOR_PING_FAIR		uiPromptTextColor
#define COLOR_PING_POOR		0xFFC06040
#define COLOR_BOTS			0xFFD08C50
#define COLOR_FULL			0xFFE0A030
#define COLOR_DIM			0xFF707070
#define COLOR_TABLE_STROKE	0xFF383838
#define COLOR_HINT			uiColorHelp

// One master query answers for both protocols, so the list is split after the
// fact: Xash3D servers carry no gs key, real GoldSrc (Steam CS 1.6) ones set
// it to 1. Keeping them apart stops the two populations from being mixed in
// one list, where half the entries can never be played.
#define TAB_XASH3D		0
#define TAB_GOLDSRC		1
#define TAB_FAVORITES	2
#define TAB_COUNT		3

#define FAVORITES_CVAR		"ui_favorites"

enum
{
	COLUMN_GOLDSRC = 0,
	COLUMN_PASSWORD,
	COLUMN_NAME,
	COLUMN_MAP,
	COLUMN_PLAYERS,
	COLUMN_BOTS,
	COLUMN_PING,
	COLUMN_COUNT
};

static int TabForInfo( const char *info )
{
	const char *gs = Info_ValueForKey( info, "gs" );

	return ( gs[0] && !stricmp( gs, "1" )) ? TAB_GOLDSRC : TAB_XASH3D;
}

// ping comes in seconds, 0 means we never heard back from the server
static unsigned int PingColor( float ping )
{
	if( ping <= 0.0f )
		return COLOR_HINT;
	if( ping < 0.100f )
		return COLOR_PING_GOOD;
	if( ping < 0.250f )
		return COLOR_PING_FAIR;
	return COLOR_PING_POOR;
}

class CMenuServerBrowser;

// centred message drawn over the table while it has no rows, so an empty list
// reads as "nothing found yet" instead of a broken panel
class CMenuListHint : public CMenuBaseItem
{
public:
	typedef CMenuBaseItem BaseClass;

	CMenuListHint() : BaseClass(), m_pOwner( NULL )
	{
		SetSize( 400, 40 );
		SetCharSize( QM_BOLDFONT );
		iFlags |= QMF_INACTIVE | QMF_DROPSHADOW;
	}

	void SetOwner( CMenuServerBrowser *owner )
	{
		m_pOwner = owner;
	}

	void Draw() override;

private:
	CMenuServerBrowser *m_pOwner;
};

class CMenuServerBrowser;

struct server_t
{
	netadr_t adr;
	char info[256];
	float ping;
	char name[64];
	char mapname[64];
	char clientsstr[64];
	char botsstr[16];
	char pingstr[64];
	bool havePassword;
	bool isLegacy;
	bool isGoldSrc;
	bool isFavorite;

	static int NameCmpAscend( const void *_a, const void *_b )
	{
		const server_t *a = (const server_t*)_a;
		const server_t *b = (const server_t*)_b;
		return colorstricmp( a->name, b->name );
	}
	static int NameCmpDescend( const void *a, const void *b )
	{
		return NameCmpAscend( b, a );
	}

	static int MapCmpAscend( const void *_a, const void *_b )
	{
		const server_t *a = (const server_t*)_a;
		const server_t *b = (const server_t*)_b;
		return stricmp( a->mapname, b->mapname );
	}
	static int MapCmpDescend( const void *a, const void *b )
	{
		return MapCmpAscend( b, a );
	}

	static int ClientCmpAscend( const void *_a, const void *_b )
	{
		const server_t *a = (const server_t*)_a;
		const server_t *b = (const server_t*)_b;

		int a_cl = atoi( Info_ValueForKey( a->info, "numcl" ));
		int b_cl = atoi( Info_ValueForKey( b->info, "numcl" ));

		if( a_cl > b_cl ) return 1;
		else if( a_cl < b_cl ) return -1;
		return 0;
	}
	static int ClientCmpDescend( const void *a, const void *b )
	{
		return ClientCmpAscend( b, a );
	}

	static int PingCmpAscend( const void *_a, const void *_b )
	{
		const server_t *a = (const server_t*)_a;
		const server_t *b = (const server_t*)_b;

		if( a->ping > b->ping ) return 1;
		else if( a->ping < b->ping ) return -1;
		return 0;
	}
	static int PingCmpDescend( const void *a, const void *b )
	{
		return PingCmpAscend( b, a );
	}

	static int BotsCmpAscend( const void *_a, const void *_b )
	{
		const server_t *a = (const server_t*)_a;
		const server_t *b = (const server_t*)_b;

		return atoi( a->botsstr ) - atoi( b->botsstr );
	}
	static int BotsCmpDescend( const void *a, const void *b )
	{
		return BotsCmpAscend( b, a );
	}
};

class CMenuGameListModel : public CMenuBaseModel
{
public:
	CMenuGameListModel( CMenuServerBrowser *parent ) : CMenuBaseModel(), parent( parent ), m_iSortingColumn(-1) {}

	void Update() override;
	int GetColumns() const override
	{
		// protocol badge, lock, name, map, players, bots, ping
		return 7;
	}
	int GetRows() const override
	{
		return servers.Count();
	}
	ECellType GetCellType( int line, int column ) override
	{
		if( column == COLUMN_PASSWORD )
			return CELL_IMAGE_ADDITIVE;
		return CELL_TEXT;
	}
	unsigned int GetAlignmentForColumn( int column ) const override
	{
		if( column == COLUMN_GOLDSRC || column == COLUMN_PASSWORD )
			return QM_CENTER;
		return QM_LEFT;
	}
	const char *GetCellText( int line, int column ) override
	{
		switch( column )
		{
		case COLUMN_GOLDSRC: return servers[line].isGoldSrc ? "GS" : "";
		case COLUMN_PASSWORD: return servers[line].havePassword ? ART_BANNER_LOCK : NULL;
		case COLUMN_NAME: return servers[line].name;
		case COLUMN_MAP: return servers[line].mapname;
		case COLUMN_PLAYERS: return servers[line].clientsstr;
		case COLUMN_BOTS: return servers[line].botsstr;
		case COLUMN_PING: return servers[line].pingstr;
		default: return NULL;
		}
	}
	bool GetLineColor( int line, unsigned int &fillColor, bool &force ) const override
	{
		// favorites get a warm tint, GoldSrc rows a cool one, so the two
		// populations stay readable even when the badge column is scrolled
		// out of sight
		unsigned int color = 0;
		bool forced = false;

		if( servers[line].isFavorite )
		{
			color = ROW_COLOR_FAVORITE;
			forced = true;
		}
		else if( servers[line].isGoldSrc )
			color = ROW_COLOR_GOLDSRC;

		if( !color )
			return false;

		fillColor = color;
		force = forced;
		return true;
	}
	bool GetCellColors(int line, int column, unsigned int &textColor, bool &force) const override
	{
		if( servers[line].isLegacy )
		{
			CColor color = uiPromptTextColor;
			color.a = color.a * 0.7;
			textColor = color;
			force = true;
			return true;
		}

		switch( column )
		{
		case COLUMN_GOLDSRC:
			if( servers[line].isGoldSrc )
			{
				textColor = COLOR_BADGE_GOLDSRC;
				force = true;
				return true;
			}
			break;
		case COLUMN_BOTS:
			// zero bots is the normal case, no need to shout it out
			if( !atoi( servers[line].botsstr ))
				textColor = COLOR_DIM;
			else
			{
				textColor = COLOR_BOTS;
				force = true;
				return true;
			}
			break;
		case COLUMN_PLAYERS:
			if( atoi( Info_ValueForKey( servers[line].info, "maxcl" )) == atoi( Info_ValueForKey( servers[line].info, "numcl" )))
			{
				textColor = COLOR_FULL;
				force = true;
				return true;
			}
			break;
		case COLUMN_PING:
			textColor = PingColor( servers[line].ping );
			force = true;
			return true;
		default:
			break;
		}

		return false;
	}

	void OnActivateEntry(int line) override;

	void Flush()
	{
		servers.RemoveAll();
		serversRefreshTime = gpGlobals->time;
	}

	bool IsHavePassword( int line )
	{
		return servers[line].havePassword;
	}

	bool IsFavorite( int line )
	{
		return servers[line].isFavorite;
	}

	void AddServerToList( netadr_t adr, const char *info );

	bool Sort(int column, bool ascend) override;

	float serversRefreshTime;
	CUtlVector<server_t> servers;
private:
	CMenuServerBrowser *parent;

	int m_iSortingColumn;
	bool m_bAscend;
};

class CMenuServerBrowser: public CMenuFramework
{
public:
	CMenuServerBrowser() : CMenuFramework( "CMenuServerBrowser" ), gameListModel( this ), m_LastFavoriteIndex( -1 ) { }
	void Draw() override;
	void Show() override;

	void SetLANOnly( bool lanOnly )
	{
		m_bLanOnly = lanOnly;
	}
	void GetGamesList( void );
	void ClearList( void );
	void RefreshList( void );
	void JoinGame( void );
	void ResetPing( void )
	{
		gameListModel.serversRefreshTime = Sys_DoubleTime();
	}

	void AddServerToList( netadr_t adr, const char *info );

	bool IsFavorite( const netadr_t &adr );
	void ToggleFavorite();

	int CurrentTab()
	{
		return tabs.GetState();
	}

	static void Connect( server_t &server );

	CMenuPicButton *joinGame;
	CMenuPicButton *createGame;
	CMenuPicButton *refresh;
	CMenuPicButton *addMaster;
	CMenuPicButton *favoriteButton;
	CMenuSwitch natOrDirect;
	CMenuSwitch tabs;
	CMenuField masterAddress;

	CMenuYesNoMessageBox msgBox;
	CMenuTable	gameList;
	CMenuGameListModel gameListModel;
	CMenuListHint listHint;

	CMenuYesNoMessageBox askPassword;
	CMenuField password;

	int	  refreshTime;
	int   refreshTime2;

	bool m_bLanOnly;
private:
	void _Init() override;
	void _VidInit() override;

	void RebuildList();
	void OnTabSwitch();
	void AddMaster();

	void LoadFavorites();
	void SaveFavorites();
	void UpdateFavoriteButton();

	static void ServerAddress( const netadr_t &adr, char *out, size_t size );
	static bool ParseAddress( const char *text, netadr_t &adr );

	// per-tab cache: a refresh refills every tab at once, so switching tabs
	// must not throw the results away and query again
	CUtlVector<server_t> m_TabServers[TAB_COUNT];
	float m_TabRefreshTime[TAB_COUNT];

	// favorites carry the info string they were added with; the cvar keeps
	// only the addresses, so a favorite seen again gets its details back
	CUtlVector<server_t> m_Favorites;

	int m_LastFavoriteIndex;
};

static server_t staticServerSelect;
static bool staticWaitingPassword = false;

ADD_MENU3( menu_internetgames, CMenuServerBrowser, UI_InternetGames_Menu );
ADD_MENU4( menu_langame, NULL, UI_LanGame_Menu, NULL );

/*
=================
CMenuServerBrowser::Menu
=================
*/
void UI_ServerBrowser_Menu( void )
{
	if ( gMenu.m_gameinfo.gamemode == GAME_SINGLEPLAYER_ONLY )
		return;

	// stop demos to allow network sockets to open
	if ( gpGlobals->demoplayback && EngFuncs::GetCvarFloat( "cl_background" ))
	{
		uiStatic.m_iOldMenuDepth = uiStatic.menu.Count();
		EngFuncs::ClientCmd( FALSE, "stop\n" );
		uiStatic.m_fDemosPlayed = true;
	}

	menu_internetgames->Show();
}

void UI_InternetGames_Menu( void )
{
	menu_internetgames->SetLANOnly( false );

	UI_ServerBrowser_Menu();
}

void UI_LanGame_Menu( void )
{
	menu_internetgames->SetLANOnly( true );

	UI_ServerBrowser_Menu();
}

bool CMenuGameListModel::Sort(int column, bool ascend)
{
	m_iSortingColumn = column;
	if( column == -1 )
		return false; // disabled

	m_bAscend = ascend;
	switch( column )
	{
	case COLUMN_GOLDSRC: return false;
	case COLUMN_PASSWORD: return false;
	case COLUMN_NAME:
		qsort( servers.Base(), servers.Count(), sizeof( server_t ),
			ascend ? server_t::NameCmpAscend : server_t::NameCmpDescend );
		return true;
	case COLUMN_MAP:
		qsort( servers.Base(), servers.Count(), sizeof( server_t ),
			ascend ? server_t::MapCmpAscend : server_t::MapCmpDescend );
		return true;
	case COLUMN_PLAYERS:
		qsort( servers.Base(), servers.Count(), sizeof( server_t ),
			ascend ? server_t::ClientCmpAscend : server_t::ClientCmpDescend );
		return true;
	case COLUMN_BOTS:
		qsort( servers.Base(), servers.Count(), sizeof( server_t ),
			ascend ? server_t::BotsCmpAscend : server_t::BotsCmpDescend );
		return true;
	case COLUMN_PING:
		qsort( servers.Base(), servers.Count(), sizeof( server_t ),
			ascend ? server_t::PingCmpAscend : server_t::PingCmpDescend );
		return true;
	}

	return false;
}

/*
=================
ParseServerInfo
=================
*/
static void ParseServerInfo( server_t &server )
{
	const char *info = server.info;

	Q_strncpy( server.name, Info_ValueForKey( info, "host" ), 64 );
	Q_strncpy( server.mapname, Info_ValueForKey( info, "map" ), 64 );
	snprintf( server.clientsstr, 64, "%s\\%s", Info_ValueForKey( info, "numcl" ), Info_ValueForKey( info, "maxcl" ) );
	Q_strncpy( server.botsstr, Info_ValueForKey( info, "bots" ), sizeof( server.botsstr ));
	snprintf( server.pingstr, 64, "%.f ms", server.ping * 1000 );

	const char *passwd = Info_ValueForKey( info, "password" );
	server.havePassword = passwd[0] && !stricmp( passwd, "1" );

	const char *legacy = Info_ValueForKey( info, "legacy" );
	server.isLegacy = legacy[0] && !stricmp( legacy, "1" );

	const char *gs = Info_ValueForKey( info, "gs" );
	server.isGoldSrc = gs[0] && !stricmp( gs, "1" );
}

/*
=================
CMenuServerBrowser::GetGamesList
=================
*/
void CMenuGameListModel::Update( void )
{
	int		i;

	// regenerate table data
	for( i = 0; i < servers.Count(); i++ )
	{
		ParseServerInfo( servers[i] );
		servers[i].isFavorite = parent->IsFavorite( servers[i].adr );
	}

	if( servers.Count() )
	{
		parent->joinGame->SetGrayed( false );
		if( m_iSortingColumn != -1 )
			Sort( m_iSortingColumn, m_bAscend );
	}
}

void CMenuGameListModel::OnActivateEntry( int line )
{
	CMenuServerBrowser::Connect( servers[line] );
}

void CMenuGameListModel::AddServerToList(netadr_t adr, const char *info)
{
	int i;

	// ignore if duplicated
	for( i = 0; i < servers.Count(); i++ )
	{
		if( !stricmp( servers[i].info, info ))
			return;
	}

	server_t server;

	server.adr = adr;
	server.ping = Sys_DoubleTime() - serversRefreshTime;
	server.ping = bound( 0, server.ping, 9.999f );
	Q_strncpy( server.info, info, sizeof( server.info ));

	// legacy servers get a halved ping so the columns stay comparable
	const char *legacy = Info_ValueForKey( info, "legacy" );
	if( legacy[0] && !stricmp( legacy, "1" ))
		server.ping /= 2;

	ParseServerInfo( server );
	servers.AddToTail( server );

	if( m_iSortingColumn != -1 )
		Sort( m_iSortingColumn, m_bAscend );
}

void CMenuServerBrowser::Connect( server_t &server )
{
	// prevent refresh during connect
	menu_internetgames->refreshTime = uiStatic.realTime + 999999;

	// ask user for password
	if( server.havePassword )
	{
		// if dialog window is still open, then user have entered the password
		if( !staticWaitingPassword )
		{
			// save current select
			staticServerSelect = server;
			staticWaitingPassword = true;

			// show password request window
			menu_internetgames->askPassword.Show();

			return;
		}
	}
	else
	{
		// remove password, as server don't require it
		EngFuncs::CvarSetString( "password", "" );
	}

	staticWaitingPassword = false;

	//BUGBUG: ClientJoin not guaranted to return, need use ClientCmd instead!!!
	//BUGBUG: But server addres is known only as netadr_t here!!!
	EngFuncs::ClientJoin( server.adr );
	UI_ConnectionProgress_Connect( "" );
}

/*
=================
CMenuServerBrowser::JoinGame
=================
*/
void CMenuServerBrowser::JoinGame()
{
	gameListModel.OnActivateEntry( gameList.GetCurrentIndex() );
}

void CMenuServerBrowser::ClearList()
{
	gameListModel.Flush();
	joinGame->SetGrayed( true );
}

void CMenuServerBrowser::RebuildList()
{
	ClearList();
	m_LastFavoriteIndex = -1;

	if( m_bLanOnly )
		return;

	int tab = tabs.GetState();

	if( tab == TAB_FAVORITES )
	{
		gameListModel.serversRefreshTime = Sys_DoubleTime();

		for( int i = 0; i < m_Favorites.Count(); i++ )
			gameListModel.AddServerToList( m_Favorites[i].adr, m_Favorites[i].info );

		joinGame->SetGrayed( m_Favorites.Count() == 0 );
		UpdateFavoriteButton();
		return;
	}

	gameListModel.serversRefreshTime = m_TabRefreshTime[tab];

	for( int i = 0; i < m_TabServers[tab].Count(); i++ )
		gameListModel.AddServerToList( m_TabServers[tab][i].adr, m_TabServers[tab][i].info );

	if( m_TabServers[tab].Count() )
		joinGame->SetGrayed( false );

	UpdateFavoriteButton();
}

/*
=================
ServerAddress
=================
*/
void CMenuServerBrowser::ServerAddress( const netadr_t &adr, char *out, size_t size )
{
	snprintf( out, size, "%d.%d.%d.%d:%u",
		adr.ip[0], adr.ip[1], adr.ip[2], adr.ip[3], (unsigned)adr.port );
}

/*
=================
ParseAddress
=================
*/
bool CMenuServerBrowser::ParseAddress( const char *text, netadr_t &adr )
{
	unsigned int ip[4];
	unsigned int port = 27015;

	if( sscanf( text, "%u.%u.%u.%u:%u", &ip[0], &ip[1], &ip[2], &ip[3], &port ) != 5 )
	{
		if( sscanf( text, "%u.%u.%u.%u", &ip[0], &ip[1], &ip[2], &ip[3] ) != 4 )
			return false;
	}

	if( port > 65535 )
		return false;

	memset( &adr, 0, sizeof( adr ));
	adr.type = NA_IP;
	adr.ip[0] = (unsigned char)ip[0];
	adr.ip[1] = (unsigned char)ip[1];
	adr.ip[2] = (unsigned char)ip[2];
	adr.ip[3] = (unsigned char)ip[3];
	adr.port = (unsigned short)port;

	return true;
}

/*
=================
IsFavorite
=================
*/
bool CMenuServerBrowser::IsFavorite( const netadr_t &adr )
{
	for( int i = 0; i < m_Favorites.Count(); i++ )
	{
		const netadr_t &fav = m_Favorites[i].adr;

		if( fav.type == adr.type && fav.port == adr.port
			&& !memcmp( fav.ip, adr.ip, sizeof( adr.ip )))
			return true;
	}

	return false;
}

/*
=================
LoadFavorites
=================
*/
void CMenuServerBrowser::LoadFavorites()
{
	m_Favorites.RemoveAll();

	const char *list = EngFuncs::GetCvarString( FAVORITES_CVAR );

	if( !list )
		return;

	char buffer[1024];
	Q_strncpy( buffer, list, sizeof( buffer ));

	char *token = buffer;
	while( token && *token )
	{
		char *next = strchr( token, ';' );
		if( next )
			*next++ = '\0';

		netadr_t adr;
		if( *token && ParseAddress( token, adr ))
		{
			server_t server;
			memset( &server, 0, sizeof( server ));
			server.adr = adr;
			server.isFavorite = true;

			// the address is all we persisted, so that is what the list shows
			// until a master query brings the real info back
			char address[64];
			ServerAddress( adr, address, sizeof( address ));
			snprintf( server.info, sizeof( server.info ), "\\host\\%s\\gamedir\\%s\\",
				address, gMenu.m_gameinfo.gamefolder );

			m_Favorites.AddToTail( server );
		}

		token = next;
	}
}

/*
=================
SaveFavorites
=================
*/
void CMenuServerBrowser::SaveFavorites()
{
	char list[1024];
	int len = 0;

	list[0] = '\0';

	for( int i = 0; i < m_Favorites.Count(); i++ )
	{
		char address[64];
		ServerAddress( m_Favorites[i].adr, address, sizeof( address ));

		int written = snprintf( list + len, sizeof( list ) - len, "%s%s",
			len ? ";" : "", address );

		if( written < 0 || len + written >= (int)sizeof( list ))
			break;

		len += written;
	}

	EngFuncs::CvarSetString( FAVORITES_CVAR, list );
}

/*
=================
ToggleFavorite
=================
*/
void CMenuServerBrowser::ToggleFavorite()
{
	int index = gameList.GetCurrentIndex();

	if( index < 0 || index >= gameListModel.GetRows() )
		return;

	const server_t &server = gameListModel.servers[index];

	for( int i = 0; i < m_Favorites.Count(); i++ )
	{
		const netadr_t &fav = m_Favorites[i].adr;

		if( fav.type == server.adr.type && fav.port == server.adr.port
			&& !memcmp( fav.ip, server.adr.ip, sizeof( server.adr.ip )))
		{
			m_Favorites.Remove( i );
			SaveFavorites();

			if( tabs.GetState() == TAB_FAVORITES )
			{
				m_LastFavoriteIndex = -1;
				RebuildList();
			}
			else
			{
				UpdateFavoriteButton();
				gameListModel.Update();
			}
			return;
		}
	}

	server_t favorite = server;
	favorite.isFavorite = true;
	m_Favorites.AddToTail( favorite );

	SaveFavorites();
	UpdateFavoriteButton();
	gameListModel.Update();
}

/*
=================
UpdateFavoriteButton
=================
*/
void CMenuServerBrowser::UpdateFavoriteButton()
{
	int index = gameList.GetCurrentIndex();
	bool isFavorite = ( index >= 0 && index < gameListModel.GetRows()
		&& gameListModel.servers[index].isFavorite );

	favoriteButton->SetPicture( isFavorite ? "gfx/shell/btn_unfavorite" : "gfx/shell/btn_favorite" );
	favoriteButton->szName = isFavorite ? L( "Remove favorite" ) : L( "Add favorite" );
}

void CMenuServerBrowser::OnTabSwitch()
{
	// GoldSrc masters never answer a NAT traversal query, so that tab stays
	// empty for as long as cl_nat is set. Fall back to direct and ask again.
	if( tabs.GetState() == TAB_GOLDSRC && EngFuncs::GetCvarFloat( "cl_nat" ) != 0.0f )
	{
		EngFuncs::CvarSetValue( "cl_nat", 0.0f );
		natOrDirect.UpdateEditable();
	}

	RebuildList();

	// a tab nobody queried yet has nothing cached: drop the rate limit so the
	// next frame refreshes instead of waiting out the timer
	if( !m_bLanOnly && m_TabServers[tabs.GetState()].Count() == 0 )
		refreshTime2 = 0;
}

void CMenuServerBrowser::AddMaster()
{
	char address[128];

	Q_strncpy( address, masterAddress.GetBuffer(), sizeof( address ));
	address[sizeof( address ) - 1] = '\0';

	// strip the spaces a soft keyboard tends to leave behind
	char *start = address;
	while( *start == ' ' ) start++;
	char *end = start + strlen( start );
	while( end > start && ( end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\n' )) *--end = '\0';

	if( !*start )
		return;

	char command[256];

	// the selected tab decides what kind of master this is
	if( tabs.GetState() == TAB_GOLDSRC )
		snprintf( command, sizeof( command ), "addmaster %s gs\n", start );
	else
		snprintf( command, sizeof( command ), "addmaster %s\n", start );

	EngFuncs::ClientCmd( FALSE, command );

	masterAddress.Clear();
	refreshTime2 = 0;
}

void CMenuServerBrowser::RefreshList()
{
	if( m_bLanOnly )
	{
		ClearList();
		EngFuncs::ClientCmd( FALSE, "localservers\n" );
		return;
	}

	for( int i = 0; i < TAB_COUNT; i++ )
	{
		m_TabServers[i].RemoveAll();
		m_TabRefreshTime[i] = Sys_DoubleTime();
	}

	ClearList();
	gameListModel.serversRefreshTime = m_TabRefreshTime[tabs.GetState()];

	if( uiStatic.realTime > refreshTime2 )
	{
		EngFuncs::ClientCmd( FALSE, "internetservers\n" );
		refreshTime2 = uiStatic.realTime + (EngFuncs::GetCvarFloat("cl_nat") ? 4000:1000);
		refresh->SetGrayed( true );
		if( uiStatic.realTime + 20000 < refreshTime )
			refreshTime = uiStatic.realTime + 20000;
	}
}

/*
=================
UI_Background_Ownerdraw
=================
*/
void CMenuServerBrowser::Draw( void )
{
	CMenuFramework::Draw();

	// the favorite button follows the row under the cursor
	int index = gameList.GetCurrentIndex();
	if( index != m_LastFavoriteIndex )
	{
		m_LastFavoriteIndex = index;
		UpdateFavoriteButton();
	}

	if( uiStatic.realTime > refreshTime )
	{
		RefreshList();
		refreshTime = uiStatic.realTime + 20000; // refresh every 10 secs
	}

	if( uiStatic.realTime > refreshTime2 )
	{
		refresh->SetGrayed( false );
	}
}

void CMenuListHint::Draw()
{
	if( !m_pOwner || !m_pOwner->IsVisible() )
		return;

	if( m_pOwner->gameListModel.GetRows() > 0 )
		return;

	const char *text = m_pOwner->CurrentTab() == TAB_FAVORITES
		? L( "No favorites yet" )
		: L( "No servers found" );

	UI_DrawString( font, m_scPos, m_scSize, text, COLOR_HINT, m_scChSize, QM_CENTER, ETF_FORCECOL );
}

/*
=================
CMenuServerBrowser::Init
=================
*/
void CMenuServerBrowser::_Init( void )
{
	AddItem( background );
	AddItem( banner );

	joinGame = AddButton( L( "Join game" ), L( "Join to selected game" ), PC_JOIN_GAME,
		VoidCb( &CMenuServerBrowser::JoinGame ), QMF_GRAYED );
	joinGame->onReleasedClActive = msgBox.MakeOpenEvent();

	createGame = AddButton( L( "GameUI_GameMenu_CreateServer" ), NULL, PC_CREATE_GAME );
	SET_EVENT_MULTI( createGame->onReleased,
	{
		if( ((CMenuServerBrowser*)pSelf->Parent())->m_bLanOnly )
			EngFuncs::CvarSetValue( "public", 0.0f );
		else EngFuncs::CvarSetValue( "public", 1.0f );

		UI_CreateGame_Menu();
	});

	// TODO: implement!
	AddButton( L( "View game info" ), L( "Get detail game info" ), PC_VIEW_GAME_INFO, CEventCallback::NoopCb, QMF_GRAYED );

	refresh = AddButton( L( "Refresh" ), L( "Refresh servers list" ), PC_REFRESH, VoidCb( &CMenuServerBrowser::RefreshList ) );

	AddButton( L( "Done" ), L( "Return to main menu" ), PC_DONE, VoidCb( &CMenuServerBrowser::Hide ) );

	addMaster = new CMenuPicButton();
	addMaster->SetNameAndStatus( L( "Add master" ), L( "Add a master server for the selected tab" ));
	addMaster->SetPicture( PC_ADD_SERVER );
	addMaster->onReleased = VoidCb( &CMenuServerBrowser::AddMaster );
	AddItem( addMaster );

	favoriteButton = new CMenuPicButton();
	favoriteButton->SetNameAndStatus( L( "Add favorite" ), L( "Keep this server in the favorites tab" ));
	favoriteButton->SetPicture( ART_BTN_FAVORITE );
	favoriteButton->onReleased = VoidCb( &CMenuServerBrowser::ToggleFavorite );
	AddItem( favoriteButton );

	listHint.SetOwner( this );

	tabs.AddSwitch( L( "Xash3D" ));
	tabs.AddSwitch( L( "GoldSrc" ));
	tabs.AddSwitch( L( "Favorites" ));
	tabs.eTextAlignment = QM_CENTER;
	tabs.bMouseToggle = true;
	tabs.iSelectColor = uiInputFgColor;
	tabs.iFgTextColor = uiInputFgColor - 0x00151515;
	// not bound to a cvar: SetCvarValue() must not push the tab index anywhere
	tabs.bUpdateImmediately = false;
	SET_EVENT_MULTI( tabs.onChanged,
	{
		CMenuSwitch *self = (CMenuSwitch*)pSelf;
		CMenuServerBrowser *parent = (CMenuServerBrowser*)self->Parent();

		parent->OnTabSwitch();
	});

	masterAddress.bHideInput = true;
	masterAddress.bAllowColorstrings = false;
	masterAddress.bNumbersOnly = false;
	masterAddress.szName = L( "Master" );
	masterAddress.iMaxLength = 64;

	msgBox.SetMessage( L( "Join a network game will exit any current game, OK to exit?" ) );
	msgBox.SetPositiveButton( L( "GameUI_OK" ), PC_OK );
	msgBox.HighlightChoice( CMenuYesNoMessageBox::HIGHLIGHT_YES );
	msgBox.onPositive = VoidCb( &CMenuServerBrowser::JoinGame );
	msgBox.Link( this );

	gameList.SetCharSize( QM_SMALLFONT );
	gameList.SetupColumn( COLUMN_GOLDSRC, NULL, 32.0f, true );
	gameList.SetupColumn( COLUMN_PASSWORD, NULL, 32.0f, true );
	gameList.SetupColumn( COLUMN_NAME, L( "Name" ), 0.40f );
	gameList.SetupColumn( COLUMN_MAP, L( "GameUI_Map" ), 0.25f );
	gameList.SetupColumn( COLUMN_PLAYERS, L( "Players" ), 100.0f, true );
	gameList.SetupColumn( COLUMN_BOTS, L( "Bots" ), 60.0f, true );
	gameList.SetupColumn( COLUMN_PING, L( "Ping" ), 120.0f, true );
	gameList.SetModel( &gameListModel );
	gameList.bFramedHintText = true;
	gameList.bAllowSorting = true;
	gameList.bDrawStroke = true;
	gameList.iStrokeWidth = UI_OUTLINE_WIDTH;
	gameList.colorStroke = COLOR_TABLE_STROKE;
	gameList.iBackgroundColor = 0xC0101010;
	gameList.iHeaderColor = COLOR_HINT;
	gameList.iStrokeFocusedColor = uiPromptTextColor;

	natOrDirect.AddSwitch( L( "Direct" ) );
	natOrDirect.AddSwitch( "NAT" );
	natOrDirect.eTextAlignment = QM_CENTER;
	natOrDirect.bMouseToggle = false;
	natOrDirect.LinkCvar( "cl_nat" );
	natOrDirect.iSelectColor = uiInputFgColor;
	// bit darker
	natOrDirect.iFgTextColor = uiInputFgColor - 0x00151515;
	SET_EVENT_MULTI( natOrDirect.onChanged,
	{
		CMenuSwitch *self = (CMenuSwitch*)pSelf;
		CMenuServerBrowser *parent = (CMenuServerBrowser*)self->Parent();

		self->WriteCvar();
		parent->ClearList();
		parent->RefreshList();
	});

	// server.dll needs for reading savefiles or startup newgame
	if( !EngFuncs::CheckGameDll( ))
		createGame->SetGrayed( true );	// server.dll is missed - remote servers only

	password.bHideInput = true;
	password.bAllowColorstrings = false;
	password.bNumbersOnly = false;
	password.szName = L( "GameUI_Password" );
	password.iMaxLength = 16;
	password.SetRect( 188, 140, 270, 32 );

	SET_EVENT_MULTI( askPassword.onPositive,
	{
		CMenuServerBrowser *parent = (CMenuServerBrowser*)pSelf->Parent();

		EngFuncs::CvarSetString( "password", parent->password.GetBuffer() );
		parent->password.Clear(); // we don't need entered password anymore
		CMenuServerBrowser::Connect( staticServerSelect );
	});

	SET_EVENT_MULTI( askPassword.onNegative,
	{
		CMenuServerBrowser *parent = (CMenuServerBrowser*)pSelf->Parent();

		EngFuncs::CvarSetString( "password", "" );
		parent->password.Clear(); // we don't need entered password anymore
		staticWaitingPassword = false;
	});

	askPassword.SetMessage( L( "GameUI_PasswordPrompt" ) );
	askPassword.Link( this );
	askPassword.Init();
	askPassword.AddItem( password );

	AddItem( gameList );
	AddItem( listHint );
	AddItem( natOrDirect );
	AddItem( tabs );
	AddItem( masterAddress );
}

/*
=================
CMenuServerBrowser::VidInit
=================
*/
void CMenuServerBrowser::_VidInit()
{
	if( m_bLanOnly )
	{
		banner.SetPicture( ART_BANNER_LAN );
		createGame->szStatusText = ( L( "Create new LAN game" ) );
natOrDirect.Hide();

		// nothing to split and nowhere to add a master on a LAN game
		tabs.Hide();
		masterAddress.Hide();
		addMaster->Hide();
		favoriteButton->Hide();
	}
	else
	{
		banner.SetPicture( ART_BANNER_INET );
		createGame->szStatusText = ( L( "Create new Internet game" ));
		natOrDirect.Show();
		tabs.Show();
		masterAddress.Show();
		addMaster->Show();
		favoriteButton->Show();
	}

	gameList.SetRect( 360, 230, -20, 465 );
	natOrDirect.SetCoord( -20 - natOrDirect.size.w, gameList.pos.y - UI_OUTLINE_WIDTH - natOrDirect.size.h );

	// tabs sit on the same row, left of the direct/nat switch
	tabs.SetCoord( 360, gameList.pos.y - UI_OUTLINE_WIDTH - tabs.size.h );

	// master address and its button go under the server table
	masterAddress.SetCoord( 360, gameList.pos.y + gameList.size.h + UI_OUTLINE_WIDTH * 2 );
	addMaster->SetCoord( 360 + masterAddress.size.w + UI_OUTLINE_WIDTH * 2,
		gameList.pos.y + gameList.size.h + UI_OUTLINE_WIDTH * 2 );
	favoriteButton->SetCoord( -20 - favoriteButton->size.w,
		gameList.pos.y + gameList.size.h + UI_OUTLINE_WIDTH * 2 );

	listHint.SetCoord( 360, gameList.pos.y + gameList.size.h / 3 );

	for( int i = 0; i < TAB_COUNT; i++ )
		m_TabRefreshTime[i] = Sys_DoubleTime();

	LoadFavorites();

	refreshTime = uiStatic.realTime + 500; // delay before update 0.5 sec
	refreshTime2 = uiStatic.realTime + 500;
}

void CMenuServerBrowser::Show()
{
	CMenuFramework::Show();

	// clear out server table
	staticWaitingPassword = false;
	for( int i = 0; i < TAB_COUNT; i++ )
	{
		m_TabServers[i].RemoveAll();
		m_TabRefreshTime[i] = Sys_DoubleTime();
	}
	gameListModel.Flush();
	gameList.DisableSorting();
	joinGame->SetGrayed( true );
}

void CMenuServerBrowser::AddServerToList(netadr_t adr, const char *info)
{
	if( stricmp( gMenu.m_gameinfo.gamefolder, Info_ValueForKey( info, "gamedir" )) != 0 )
		return;

	if( !WasInit() )
		return;

	if( !IsVisible() )
		return;

	if( m_bLanOnly )
	{
		gameListModel.AddServerToList( adr, info );
		joinGame->SetGrayed( false );
		return;
	}

	int tab = TabForInfo( info );
	CUtlVector<server_t> &list = m_TabServers[tab];
	int i;

	// a favorite seen in a master query keeps its entry fresh
	for( i = 0; i < m_Favorites.Count(); i++ )
	{
		const netadr_t &fav = m_Favorites[i].adr;

		if( fav.type == adr.type && fav.port == adr.port
			&& !memcmp( fav.ip, adr.ip, sizeof( adr.ip )))
		{
			Q_strncpy( m_Favorites[i].info, info, sizeof( m_Favorites[i].info ));
			break;
		}
	}

	// ignore if duplicated
	for( i = 0; i < list.Count(); i++ )
	{
		if( !stricmp( list[i].info, info ))
			return;
	}

	server_t server;

	memset( &server, 0, sizeof( server ));
	server.adr = adr;
	server.ping = Sys_DoubleTime() - m_TabRefreshTime[tab];
	server.ping = bound( 0, server.ping, 9.999f );
	Q_strncpy( server.info, info, sizeof( server.info ));
	server.isGoldSrc = ( tab == TAB_GOLDSRC );

	list.AddToTail( server );

	// the other tab keeps it cached, this one shows it right away
	if( tabs.GetState() == tab )
	{
		gameListModel.AddServerToList( adr, info );
		joinGame->SetGrayed( false );
	}
}

/*
=================
UI_AddServerToList
=================
*/
void UI_AddServerToList( netadr_t adr, const char *info )
{
	if( !menu_internetgames )
		return;

	menu_internetgames->AddServerToList( adr, info );
}

/*
=================
UI_MenuResetPing_f
=================
*/
void UI_MenuResetPing_f( void )
{
	Con_Printf("UI_MenuResetPing_f\n");
	if( menu_internetgames )
		menu_internetgames->ResetPing();
}
ADD_COMMAND( menu_resetping, UI_MenuResetPing_f );
