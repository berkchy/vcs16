// bhop.cpp — hold-to-bhop assist and +gs ground strafe for the stock client.
//
// BunnyHop: while +jump is held, IN_JUMP stays set on the ground and during
// the last qsi_bhop_ground_dist units above it, and is cleared everywhere
// else in the air. The cleared bit is what makes the server jump again on
// the landing tick, so holding jump chain-hops without re-pressing it.
// With the assist off the classic behaviour stays: one press, one jump.
//
// GroundStrafe: while +gs is held, duck is tapped for exactly one tick on
// each landing (and released the next tick), which keeps the speed a
// duck-tap strafe would give. Duck is forced off in the air between taps.

#include "hud.h"
#include "cl_util.h"
#include "usercmd.h"
#include "const.h"
#include "in_defs.h"
#include "pmtrace.h"
#include "pm_defs.h"
#include "bhop.h"
#include "com_weapons.h"

extern cl_enginefunc_t gEngfuncs;

static cvar_t *s_bhopAssist = NULL;
static cvar_t *s_bhopGroundDist = NULL;
static int s_gs = 0;

static void GS_On( void ) { s_gs = 1; }
static void GS_Off( void ) { s_gs = 0; }

void BHOP_Init( void )
{
	s_bhopAssist = gEngfuncs.pfnRegisterVariable( "qsi_bhop_assist", "1", FCVAR_ARCHIVE );
	s_bhopGroundDist = gEngfuncs.pfnRegisterVariable( "qsi_bhop_ground_dist", "24.0", FCVAR_ARCHIVE );
	gEngfuncs.pfnAddCommand( "+gs", GS_On );
	gEngfuncs.pfnAddCommand( "-gs", GS_Off );
	s_gs = 0;
}

static bool BHOP_IsNearGround( cl_entity_t *local, float distance )
{
	if( !local )
		return false;

	float traceDist = distance > 4.0f ? distance : 4.0f;
	vec3_t start, end;
	VectorCopy( local->origin, start );
	VectorCopy( local->origin, end );
	start[2] += 1.0f;
	end[2] -= traceDist;

	pmtrace_t *tr = gEngfuncs.PM_TraceLine( start, end, PM_TRACELINE_PHYSENTSONLY, 2, local->index );
	if( !tr )
		return false;

	return tr->fraction < 1.0f && !tr->allsolid;
}

static void BHOP_Apply( usercmd_t *cmd )
{
	if( !( cmd->buttons & IN_JUMP ))
		return;

	if( g_iPlayerFlags & FL_ONGROUND )
		return;

	bool assist = !s_bhopAssist || s_bhopAssist->value >= 0.5f;
	float dist = s_bhopGroundDist ? s_bhopGroundDist->value : 24.0f;
	if( dist < 4.0f ) dist = 4.0f;
	if( dist > 64.0f ) dist = 64.0f;

	if( assist )
	{
		cl_entity_t *local = gEngfuncs.GetLocalPlayer();
		if( BHOP_IsNearGround( local, dist ))
			return;
	}

	cmd->buttons &= ~IN_JUMP;
}

static void GS_Apply( usercmd_t *cmd )
{
	static bool wasOnGround = false;
	static bool releaseDuckNextFrame = false;
	static bool wasEnabled = false;
	static bool armedForLanding = false;

	bool onGround = ( g_iPlayerFlags & FL_ONGROUND ) != 0;

	if( !s_gs )
	{
		wasOnGround = onGround;
		releaseDuckNextFrame = false;
		wasEnabled = false;
		armedForLanding = false;
		return;
	}

	bool activatedThisFrame = !wasEnabled;

	if( !wasEnabled )
	{
		// Ignore mid-air activation; only arm immediately if +gs was pressed on ground.
		armedForLanding = onGround;
	}

	if( releaseDuckNextFrame )
	{
		cmd->buttons &= ~IN_DUCK;
		releaseDuckNextFrame = false;
	}

	if( activatedThisFrame && onGround )
	{
		cmd->buttons |= IN_DUCK;
		releaseDuckNextFrame = true;
		armedForLanding = true;
	}
	else if( onGround && !wasOnGround )
	{
		if( armedForLanding )
		{
			cmd->buttons |= IN_DUCK;
			releaseDuckNextFrame = true;
		}
		else
		{
			// First landing after enabling in air should not trigger the tap.
			armedForLanding = true;
		}
	}
	else if( onGround )
	{
		armedForLanding = true;
	}
	else if( !releaseDuckNextFrame )
	{
		cmd->buttons &= ~IN_DUCK;
	}

	wasEnabled = true;
	wasOnGround = onGround;
}

void BHOP_CreateMove( struct usercmd_s *cmd )
{
	if( !cmd )
		return;

	BHOP_Apply((usercmd_t *)cmd );
	GS_Apply((usercmd_t *)cmd );
}
