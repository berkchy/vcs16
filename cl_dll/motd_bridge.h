/*
========================
motd_bridge.h

Platform HTML MOTD, reached without touching cl_enginefunc_t.

The client dll receives that struct by value (gEngfuncs = *pEnginefuncs), so a
dll built against a header with more fields than the running engine reads past
the end of the engine's table and calls garbage. The MOTD entry points
therefore come from mobile_engfuncs_t::pfnGetNativeObject("MOTDAPI"), which
exists in every engine that has it and yields NULL in every engine that has
not. An older engine APK keeps the game fully playable: the MOTD is drawn with
the HUD text renderer instead of a WebView.
========================
*/
#ifndef MOTD_BRIDGE_H
#define MOTD_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

// Ask the engine to show an HTML MOTD in a platform dialog (a sandboxed
// WebView on Android, nothing anywhere else). Returns true only when the
// dialog really is on screen; false means "no dialog here", and the caller
// must render the MOTD itself.
int MOTDAPI_Show( const char *html );

// true while the platform MOTD dialog is up.
int MOTDAPI_IsActive( void );

#ifdef __cplusplus
}
#endif

#endif // MOTD_BRIDGE_H
