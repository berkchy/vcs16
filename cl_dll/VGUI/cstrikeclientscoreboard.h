// cstrikeclientscoreboard.h - Steam-style Counter-Strike scoreboard,
// VGUI2 implementation. Included only by the bridge TU (never together
// with game headers).

#ifndef CSTRIKECLIENTSCOREBOARD_H
#define CSTRIKECLIENTSCOREBOARD_H

#include <vgui_controls/Frame.h>
#include <vgui_controls/Label.h>

#include "cs_scoreboard_bridge.h"

class CCStrikeClientScoreBoard : public vgui2::Frame
{
	typedef vgui2::Frame BaseClass;
public:
	CCStrikeClientScoreBoard();
	~CCStrikeClientScoreBoard();

	// Rebuild/refresh contents from a fresh snapshot. Shows nothing when
	// board is empty; callers control visibility separately.
	void SetBoard( const csb_board_t *board );

	// Singleton for the bridge functions.
	static CCStrikeClientScoreBoard *Get();
	static void Shutdown();

private:
	struct Row
	{
		vgui2::Label *name;
		vgui2::Label *kills;
		vgui2::Label *deaths;
		vgui2::Label *ping;
	};

	void EnsureRows( int count );

	vgui2::Label *m_pTitle;
	vgui2::Label *m_pColName;
	vgui2::Label *m_pColKills;
	vgui2::Label *m_pColDeaths;
	vgui2::Label *m_pColPing;
	vgui2::Label *m_pTeamA;
	vgui2::Label *m_pTeamB;
	vgui2::Label *m_pSpectators;

	Row m_rows[CSB_MAX_PLAYERS];
	int m_rowCount;

	static CCStrikeClientScoreBoard *s_instance;
};

#endif // CSTRIKECLIENTSCOREBOARD_H
