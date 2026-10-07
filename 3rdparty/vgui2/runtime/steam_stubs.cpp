// Steam API stubs for the static VGUI2 port.
// Xash3D has no Steam runtime; the vgui2 HTML browser control is inert.
// A real Steam client library could be dropped in later; keep the ABI intact.

#include "steam/steam_api.h"
#include "steam/steamtypes.h"

S_API HSteamPipe SteamAPI_GetHSteamPipe()
{
	return 0;
}

S_API HSteamUser SteamAPI_GetHSteamUser()
{
	return 0;
}

S_API void S_CALLTYPE SteamAPI_RegisterCallback( class CCallbackBase *pCallback, int iCallback )
{
}

S_API void S_CALLTYPE SteamAPI_UnregisterCallback( class CCallbackBase *pCallback )
{
}

S_API void S_CALLTYPE SteamAPI_RegisterCallResult( class CCallbackBase *pCallback, SteamAPICall_t hAPICall )
{
}

S_API void S_CALLTYPE SteamAPI_UnregisterCallResult( class CCallbackBase *pCallback, SteamAPICall_t hAPICall )
{
}

S_API ISteamClient *S_CALLTYPE SteamClient()
{
	return nullptr;
}