// csloadingpanel.h - the VGUI2 loading screen, and the bridge functions
// around it. Included only by the bridge TU (never together with game
// headers), exactly like cstrikeclientscoreboard.h.

#ifndef CSLOADINGPANEL_H
#define CSLOADINGPANEL_H

#include <vgui_controls/Frame.h>
#include <vgui_controls/Label.h>

#include "cs_scoreboard_bridge.h"

class CSLoadingPanel : public vgui2::Frame
{
	typedef vgui2::Frame BaseClass;
public:
	CSLoadingPanel();
	~CSLoadingPanel();

	// Fresh load state, and the moment it arrived. The time matters because
	// with CSB_PROGRESS_UNKNOWN the bar sweeps, and the sweep has to start
	// from the left every time rather than from wherever the last load left it.
	void SetLoading( const csb_loading_t *info, double now );

	// Take the screen down. Static because the engine stops calling the hook
	// the moment the load ends, so this ends up running from HUD_Redraw where
	// no instance is at hand - and it must not build one, which is what a
	// per-frame caller on a client that never loaded VGUI2 would otherwise get.
	static void SetLoadingDone();

	static CSLoadingPanel *Get();
	static void Shutdown();

protected:
	// The bar is drawn here rather than with a progress control: ISurface
	// already carries the few calls it needs, and a sweep that follows the
	// frame clock has to repaint itself every frame anyway.
	virtual void Paint();

private:
	vgui2::Label *m_pMap;
	vgui2::Label *m_pHint;

	double m_startTime;
	int m_lastProgress;
	bool m_unknown;

	static CSLoadingPanel *s_instance;
};

#endif // CSLOADINGPANEL_H