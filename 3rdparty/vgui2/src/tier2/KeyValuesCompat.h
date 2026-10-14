#ifndef GAME_CLIENT_KEYVALUESCOMPAT_H
#define GAME_CLIENT_KEYVALUESCOMPAT_H

#include "interface.h"

// This is the VGUI2 version of the IKeyValuesSystem interface. It has
// additional methods that make it incompatible. - Solokiller
//
// It lives here rather than in the .cpp because the client bootstrap answers
// "KeyValues003" from a CreateInterfaceFn of its own, and handing a pointer to
// a type it cannot name through a void* would be a cast that hides a
// mismatch between the two definitions.
class IKeyValues : public IBaseInterface
{
public:
	// registers the size of the KeyValues in the specified instance
	// so it can build a properly sized memory pool for the KeyValues objects
	// the sizes will usually never differ but this is for versioning safety
	virtual void RegisterSizeofKeyValues( int size ) = 0;

	// allocates/frees a KeyValues object from the shared mempool
	virtual void *AllocKeyValuesMemory( int size ) = 0;
	virtual void FreeKeyValuesMemory( void *pMem ) = 0;

	// symbol table access (used for key names)
	virtual HKeySymbol GetSymbolForString( const char *name ) = 0;
	virtual const char *GetStringForSymbol( HKeySymbol symbol ) = 0;

	// Used by GoldSource only. - Solokiller
	virtual void GetLocalizedFromANSI( const char* ansi, wchar_t* outBuf, int unicodeBufferSizeInBytes ) = 0;
	virtual void GetANSIFromLocalized( const wchar_t* wchar, char* outBuf, int ansiBufferSizeInBytes ) = 0;

	// for debugging, adds KeyValues record into global list so we can track memory leaks
	virtual void AddKeyValuesToMemoryLeakList( void *pMem, HKeySymbol name ) = 0;
	virtual void RemoveKeyValuesFromMemoryLeakList( void *pMem ) = 0;
};

#define IKEYVALUES_INTERFACE_VERSION "KeyValues003"

/**
*	Initializes the KeyValuesSystem accessor.
*	@return Whether an engine-provided KeyValues003 is in use. Not a success
*	report: see KV_GetBuiltInKeyValues.
*/
bool KV_InitKeyValuesSystem( CreateInterfaceFn* pFactories, int iNumFactories );

/**
*	The self-contained IKeyValues this file falls back to when no factory hands
*	over a KeyValues003.
*
*	Exposed so the bootstrap can answer that name from a CreateInterfaceFn of its
*	own. ConnectTier2Libraries treats KV_InitKeyValuesSystem returning false as
*	fatal - it calls Error(), which aborts - so "no engine KeyValues003" cannot
*	be left to the fallback branch. Handing this over as a second factory keeps
*	tier2 on the path it expects, with the same working implementation.
*/
IKeyValues *KV_GetBuiltInKeyValues( void );

#endif //GAME_CLIENT_KEYVALUESCOMPAT_H