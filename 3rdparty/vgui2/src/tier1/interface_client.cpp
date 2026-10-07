// interface_client.cpp - minimal Sys_* shim for the CS client build.
//
// common/interface.cpp (built into the client) already provides
// InterfaceReg, CreateInterface, GetModuleHandle, Sys_UnloadModule,
// Sys_GetFactoryThis and Sys_GetFactory. The full tier1/interface.cpp
// must stay OUT of this build (duplicate symbols), so only the pieces
// vgui2 actually needs and common/ lacks are defined here:
//   Sys_LoadModule (tier1's two-arg form used by steam_api.cpp)

#include <tier1/interface.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

CSysModule *Sys_LoadModule( const char *pModuleName, Sys_Flags flags )
{
	(void)flags;
	if ( !pModuleName || !pModuleName[0] )
		return NULL;
#ifdef _WIN32
	HMODULE hDLL = LoadLibraryA( pModuleName );
#else
	void *hDLL = dlopen( pModuleName, RTLD_NOW );
#endif
	return reinterpret_cast<CSysModule *>( hDLL );
}

CreateInterfaceFn Sys_GetFactory( CSysModule *pModule )
{
	if ( !pModule )
		return NULL;
#ifdef _WIN32
	return reinterpret_cast<CreateInterfaceFn>( GetProcAddress( reinterpret_cast<HMODULE>( pModule ), CREATEINTERFACE_PROCNAME ));
#else
	return reinterpret_cast<CreateInterfaceFn>( dlsym( reinterpret_cast<void *>( pModule ), CREATEINTERFACE_PROCNAME ));
#endif
}
