// tier0 debug/spew overrides for the static VGUI2 port
// Minimal console-based implementations. The engine can install a SpewOutputFunc
// to route messages into its own console.

#include <cstdio>
#include <cstdarg>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

#include "tier0/dbg.h"

static SpewOutputFunc_t s_pSpewOutputFunc = nullptr;

void _AssertValidReadPtr( void *ptr, int count )
{
}

void _AssertValidWritePtr( void *ptr, int count )
{
}

void AssertValidStringPtr( const char *ptr, int maxchar )
{
}

#ifdef _WIN32
bool Plat_IsInDebugSession()
{
	return IsDebuggerPresent() != FALSE;
}
#endif

void SpewOutputFunc( SpewOutputFunc_t func )
{
	s_pSpewOutputFunc = func;
}

SpewOutputFunc_t GetSpewOutputFunc( void )
{
	return s_pSpewOutputFunc;
}

void SpewActivate( const char *pGroupName, int level )
{
}

bool IsSpewActive( const char *pGroupName, int level )
{
	return false;
}

void _SpewInfo( SpewType_t type, const char *pFile, int line )
{
	const char *pType = "Message";
	switch ( type )
	{
	case SPEW_WARNING: pType = "Warning"; break;
	case SPEW_ASSERT: pType = "Assert"; break;
	case SPEW_ERROR: pType = "Error"; break;
	case SPEW_LOG: pType = "Log"; break;
	default: break;
	}
	if ( pFile && *pFile )
		fprintf( stderr, "[%s] %s(%d) : ", pType, pFile, line );
}

SpewRetval_t _SpewMessage( const char *pMsg, ... )
{
	char buf[1024];
	va_list args;
	va_start( args, pMsg );
	vsnprintf( buf, sizeof( buf ), pMsg, args );
	va_end( args );

	if ( s_pSpewOutputFunc )
		s_pSpewOutputFunc( SPEW_MESSAGE, buf );
	else
		fputs( buf, stderr );
	return SPEW_CONTINUE;
}

SpewRetval_t _DSpewMessage( const char *pGroupName, int level, const char *pMsg, ... )
{
	char buf[1024];
	va_list args;
	va_start( args, pMsg );
	vsnprintf( buf, sizeof( buf ), pMsg, args );
	va_end( args );

	if ( s_pSpewOutputFunc )
		s_pSpewOutputFunc( SPEW_MESSAGE, buf );
	else
		fputs( buf, stderr );
	return SPEW_CONTINUE;
}

void Msg( const char *pMsg, ... )
{
	char buf[1024];
	va_list args;
	va_start( args, pMsg );
	vsnprintf( buf, sizeof( buf ), pMsg, args );
	va_end( args );

	if ( s_pSpewOutputFunc )
		s_pSpewOutputFunc( SPEW_MESSAGE, buf );
	else
		fputs( buf, stderr );
}

void Warning( const char *pMsg, ... )
{
	char buf[1024];
	va_list args;
	va_start( args, pMsg );
	vsnprintf( buf, sizeof( buf ), pMsg, args );
	va_end( args );

	if ( s_pSpewOutputFunc )
		s_pSpewOutputFunc( SPEW_WARNING, buf );
	else
		fprintf( stderr, "WARNING: %s", buf );
}

void DWarning( const char *pGroupName, int level, const char *pMsg, ... )
{
	char buf[1024];
	va_list args;
	va_start( args, pMsg );
	vsnprintf( buf, sizeof( buf ), pMsg, args );
	va_end( args );

	if ( s_pSpewOutputFunc )
		s_pSpewOutputFunc( SPEW_WARNING, buf );
	else
		fprintf( stderr, "WARNING: %s", buf );
}

void Error( const char *pMsg, ... )
{
	char buf[1024];
	va_list args;
	va_start( args, pMsg );
	vsnprintf( buf, sizeof( buf ), pMsg, args );
	va_end( args );

	if ( s_pSpewOutputFunc )
		s_pSpewOutputFunc( SPEW_ERROR, buf );
	else
		fprintf( stderr, "ERROR: %s", buf );
	abort();
}

bool ShouldUseNewAssertDialog()
{
	return false;
}

bool DoNewAssertDialog( const char *pFile, int line, const char *pExpression )
{
	return false;
}

void _ExitOnFatalAssert( const char *pFile, int line )
{
	fprintf( stderr, "Fatal assertion failed at %s(%d)\n", pFile ? pFile : "?", line );
	abort();
}