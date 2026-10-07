#include <stdlib.h>
#include <string.h>

#include "interface.h"
#include "vstdlib/IKeyValuesSystem.h"
#include "KeyValues.h"
#include "KeyValuesCompat.h"

//This is the VGUI2 version of the IKeyValuesSystem interface. It has additional methods that makes it incompatible. - Solokiller
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

	//Used by GoldSource only. - Solokiller
	virtual void GetLocalizedFromANSI( const char* ansi, wchar_t* outBuf, int unicodeBufferSizeInBytes ) = 0;

	virtual void GetANSIFromLocalized( const wchar_t* wchar, char* outBuf, int ansiBufferSizeInBytes ) = 0;

	// for debugging, adds KeyValues record into global list so we can track memory leaks
	virtual void AddKeyValuesToMemoryLeakList( void *pMem, HKeySymbol name ) = 0;
	virtual void RemoveKeyValuesFromMemoryLeakList( void *pMem ) = 0;
};

#define IKEYVALUES_INTERFACE_VERSION "KeyValues003"

IKeyValues* g_pKeyValuesInterface = NULL;

//-----------------------------------------------------------------------------
// Self-contained fallback implementation.
//
// The wrapper below only *forwards* to an IKeyValues ("KeyValues003")
// implementation that a factory has to hand over first (game UI modules do
// that from ConnectTier2Libraries()). Anything that creates a KeyValues before
// that happened used to dereference the NULL interface pointer - the classic
// SIGSEGV at 0x0 inside AllocKeyValuesMemory, e.g. when the engine-side VGUI2
// host parses its scheme during CL_InitLocal, long before any client module
// exists.
//
// To make the wrapper safe on its own it falls back to this implementation.
// Both allocate with malloc/free, and the first implementation that gets picked
// stays for the lifetime of the process, so a key name symbol can never be
// looked up in two different symbol tables.
//-----------------------------------------------------------------------------
static char *KV_StrDup( const char *str )
{
	if ( !str )
		return NULL;

	size_t len = strlen( str ) + 1;
	char *copy = (char *)malloc( len );
	if ( copy )
		memcpy( copy, str, len );

	return copy;
}

class CFallbackKeyValuesSystem : public IKeyValues
{
public:
	CFallbackKeyValuesSystem()
	{
		m_size = 0;
		m_symbols.AddToTail( KV_StrDup( "" )); // symbol 0 is always the empty string
	}

	~CFallbackKeyValuesSystem()
	{
		for ( int i = 0; i < m_symbols.Count(); i++ )
			free( m_symbols[i] );
	}

	void RegisterSizeofKeyValues( int size ) override
	{
		if ( size > m_size )
			m_size = size;
	}

	void *AllocKeyValuesMemory( int size ) override
	{
		if ( size <= 0 )
			size = m_size;
		if ( size <= 0 )
			size = sizeof( KeyValues );

		return malloc((size_t)size );
	}

	void FreeKeyValuesMemory( void *pMem ) override
	{
		free( pMem );
	}

	HKeySymbol GetSymbolForString( const char *name ) override
	{
		if ( !name )
			return INVALID_KEY_SYMBOL;

		for ( int i = 0; i < m_symbols.Count(); i++ )
		{
			if ( m_symbols[i] && !Q_stricmp( m_symbols[i], name ))
				return (HKeySymbol)i;
		}

		m_symbols.AddToTail( KV_StrDup( name ));
		return (HKeySymbol)( m_symbols.Count() - 1 );
	}

	const char *GetStringForSymbol( HKeySymbol symbol ) override
	{
		if ( symbol < 0 || symbol >= m_symbols.Count() || !m_symbols[symbol] )
			return "";
		return m_symbols[symbol];
	}

	void GetLocalizedFromANSI( const char *ansi, wchar_t *outBuf, int unicodeBufferSizeInBytes ) override
	{
		// Without the engine interface neither the encoding table nor the
		// localize interface is available: widen as ASCII, which is what
		// GoldSource schedules from its .res files anyway.
		if ( !outBuf || unicodeBufferSizeInBytes <= 0 )
			return;

		int i = 0;
		for ( ; ansi && ansi[i] && ( i + 1 ) * (int)sizeof( wchar_t ) <= unicodeBufferSizeInBytes; i++ )
			outBuf[i] = (wchar_t)(unsigned char)ansi[i];
		outBuf[i] = 0;
	}

	void GetANSIFromLocalized( const wchar_t *wchar, char *outBuf, int ansiBufferSizeInBytes ) override
	{
		if ( !outBuf || ansiBufferSizeInBytes <= 0 )
			return;

		int i = 0;
		for ( ; wchar && wchar[i] && i + 1 < ansiBufferSizeInBytes; i++ )
			outBuf[i] = (char)wchar[i];
		outBuf[i] = 0;
	}

	void AddKeyValuesToMemoryLeakList( void *pMem, HKeySymbol name ) override
	{
		(void)name;
		if ( pMem )
			m_leaks.AddToTail( pMem );
	}

	void RemoveKeyValuesFromMemoryLeakList( void *pMem ) override
	{
		m_leaks.FindAndRemove( pMem );
	}

private:
	int m_size;
	CUtlVector<char *> m_symbols;
	CUtlVector<void *> m_leaks;
};

static CFallbackKeyValuesSystem s_FallbackKeyValuesSystem;

// Single access point for the wrapper: the interface pointer can never be NULL
// here, no matter in which order the engine and the game modules initialize.
static IKeyValues *KeyValuesInterface()
{
	if ( !g_pKeyValuesInterface )
		g_pKeyValuesInterface = &s_FallbackKeyValuesSystem;

	return g_pKeyValuesInterface;
}

class CKeyValuesWrapper : public IKeyValuesSystem
{
public:
	void RegisterSizeofKeyValues( int size ) override
	{
		return KeyValuesInterface()->RegisterSizeofKeyValues( size );
	}

	void *AllocKeyValuesMemory( int size ) override
	{
		return KeyValuesInterface()->AllocKeyValuesMemory( size );
	}

	void FreeKeyValuesMemory( void *pMem ) override
	{
		KeyValuesInterface()->FreeKeyValuesMemory( pMem );
	}

	HKeySymbol GetSymbolForString( const char *name ) override
	{
		return KeyValuesInterface()->GetSymbolForString( name );
	}

	const char *GetStringForSymbol( HKeySymbol symbol ) override
	{
		return KeyValuesInterface()->GetStringForSymbol( symbol );
	}

	void AddKeyValuesToMemoryLeakList( void *pMem, HKeySymbol name ) override
	{
		KeyValuesInterface()->AddKeyValuesToMemoryLeakList( pMem, name );
	}

	void RemoveKeyValuesFromMemoryLeakList( void *pMem ) override
	{
		KeyValuesInterface()->RemoveKeyValuesFromMemoryLeakList( pMem );
	}
};

CKeyValuesWrapper g_KeyValuesSystem;

IKeyValuesSystem *keyvalues()
{
	return &g_KeyValuesSystem;
}

bool KV_InitKeyValuesSystem( CreateInterfaceFn* pFactories, int iNumFactories )
{
	for (int i = 0; i < iNumFactories; ++i)
	{
		if (!g_pKeyValuesInterface)
		{
			g_pKeyValuesInterface = (IKeyValues *)pFactories[i](IKEYVALUES_INTERFACE_VERSION, NULL);
		}
	}

	if( !g_pKeyValuesInterface )
		g_pKeyValuesInterface = &s_FallbackKeyValuesSystem;

	g_pKeyValuesInterface->RegisterSizeofKeyValues( sizeof( KeyValues ) );

	// Report whether an engine provided system is in use: callers that treat a
	// missing KeyValues003 as "no VGUI2 support" must keep that behaviour, even
	// though the fallback above keeps KeyValues usable.
	return g_pKeyValuesInterface != &s_FallbackKeyValuesSystem;
}
