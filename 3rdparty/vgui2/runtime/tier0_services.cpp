// tier0_service overrides for the static VGUI2 port
// Provides the tier0 runtime services that BugfixedHL links from a prebuilt
// libtier0.so. In Xash3D these are compiled directly into the engine.

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "tier1/strtools.h"
#include "tier1/utlvector.h"
#include "tier1/utlstring.h"
#include "vstdlib/random.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

//-----------------------------------------------------------------------------
// Time
//-----------------------------------------------------------------------------
#ifdef _WIN32
#include <windows.h>
double Plat_FloatTime( void )
{
	static LARGE_INTEGER s_freq = { 0 };
	LARGE_INTEGER now;
	if ( !s_freq.QuadPart )
		QueryPerformanceFrequency( &s_freq );
	QueryPerformanceCounter( &now );
	if ( !s_freq.QuadPart )
		return 0.0;
	return (double)now.QuadPart / (double)s_freq.QuadPart;
}
#elif defined( POSIX )
double Plat_FloatTime( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}
#else
#include <sys/time.h>
double Plat_FloatTime( void )
{
	struct timeval tv;
	gettimeofday( &tv, NULL );
	return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}
#endif

//-----------------------------------------------------------------------------
// Random number services (vstdlib/random.h)
//-----------------------------------------------------------------------------
static unsigned int s_randSeed = 0;

void RandomSeed( int iSeed )
{
	s_randSeed = (unsigned int)iSeed;
}

float RandomFloat( float flMinVal, float flMaxVal )
{
	if ( flMaxVal <= flMinVal )
		return flMinVal;

	unsigned int next = s_randSeed * 1103515245 + 12345;
	s_randSeed = next;
	return flMinVal + (float)( ( next >> 16 ) & 0x7fff ) / 32767.0f * ( flMaxVal - flMinVal );
}

int RandomInt( int iMinVal, int iMaxVal )
{
	if ( iMaxVal <= iMinVal )
		return iMinVal;

	unsigned int next = s_randSeed * 1103515245 + 12345;
	s_randSeed = next;
	return iMinVal + (int)( ( ( next >> 16 ) & 0x7fff ) % ( unsigned int )( iMaxVal - iMinVal + 1 ) );
}

float RandomGaussianFloat( float flMean, float flStdDev )
{
	// Box-Muller approximation using the same LCG stream
	float x1 = RandomFloat( 0.0f, 1.0f );
	float x2 = RandomFloat( 0.0f, 1.0f );
	if ( x1 <= 1e-6f )
		x1 = 1e-6f;
	float z = sqrtf( -2.0f * logf( x1 ) ) * cosf( 2.0f * PI * x2 );
	return flMean + z * flStdDev;
}

//-----------------------------------------------------------------------------
// Command line interface (tier0/icommandline.h)
//-----------------------------------------------------------------------------
#include "tier0/icommandline.h"

namespace
{
class CCommandLineImpl : public ICommandLine
{
public:
	CCommandLineImpl()
	{
		Reset();
	}

	void Reset()
	{
		m_Parms.RemoveAll();
		const char *env = getenv( "HL1_CLIENT_ARGS" );
		if ( env && *env )
			SetCommandLine( env );
	}

	const char *GetCmdAsOneString() const
	{
		static char s_Buf[4096];
		s_Buf[0] = 0;
		for ( int i = 0; i < m_Parms.Count(); i++ )
		{
			if ( i )
				strncat( s_Buf, " ", sizeof( s_Buf ) - strlen( s_Buf ) - 1 );
			strncat( s_Buf, m_Parms[i].String(), sizeof( s_Buf ) - strlen( s_Buf ) - 1 );
		}
		return s_Buf;
	}

	bool SetCommandLine( const char *commandline )
	{
		m_Parms.RemoveAll();
		char work[4096];
		Q_strncpy( work, commandline, sizeof( work ) );

		char *p = work;
		while ( *p )
		{
			char *token = p;
			while ( *p && *p != ' ' && *p != '\t' )
				p++;
			if ( *p )
			{
				*p++ = 0;
				while ( *p == ' ' || *p == '\t' )
					p++;
			}
			m_Parms.AddToTail( token ? token : "" );
		}
		return true;
	}

	virtual void CreateCmdLine( const char *commandline ) override { SetCommandLine( commandline ); }
	virtual void CreateCmdLine( int argc, char **argv ) override
	{
		m_Parms.RemoveAll();
		for ( int i = 0; i < argc; i++ )
			m_Parms.AddToTail( argv ? argv[i] : "" );
	}
	virtual const char *GetCmdLine( void ) const override { return GetCmdAsOneString(); }
	virtual const char *CheckParm( const char *psz, const char **ppszValue = 0 ) const override
	{
		for ( int i = 0; i < m_Parms.Count(); i++ )
		{
			if ( Q_strcasecmp( m_Parms[i].String(), psz ) == 0 )
			{
				if ( ppszValue )
					*ppszValue = ( i + 1 < m_Parms.Count() ) ? m_Parms[i + 1].String() : "";
				return m_Parms[i].String();
			}
		}
		return 0;
	}
	virtual void RemoveParm( const char *parm ) override
	{
		for ( int i = m_Parms.Count() - 1; i >= 0; i-- )
		{
			if ( Q_strcasecmp( m_Parms[i].String(), parm ) == 0 )
				m_Parms.Remove( i );
		}
	}
	virtual void AppendParm( const char *pszParm, const char *pszValues ) override
	{
		m_Parms.AddToTail( pszParm );
		if ( pszValues && *pszValues )
			m_Parms.AddToTail( pszValues );
	}
	virtual const char *ParmValue( const char *psz, const char *pDefaultVal = 0 ) const override
	{
		const char *pValue = nullptr;
		CheckParm( psz, &pValue );
		if ( pValue == nullptr )
			return pDefaultVal;
		if ( pValue[0] == 0 )
			return pDefaultVal;
		return pValue;
	}
	virtual int ParmValue( const char *psz, int nDefaultVal ) const override
	{
		const char *str = ParmValue( psz, (const char *)nullptr );
		if ( !str )
			return nDefaultVal;
		return atoi( str );
	}
	virtual float ParmValue( const char *psz, float flDefaultVal ) const override
	{
		const char *str = ParmValue( psz, (const char *)nullptr );
		if ( !str )
			return flDefaultVal;
		return (float)atof( str );
	}
	virtual int ParmCount() const override { return m_Parms.Count(); }
	virtual int FindParm( const char *psz ) const override
	{
		for ( int i = 0; i < m_Parms.Count(); i++ )
		{
			if ( Q_strcasecmp( m_Parms[i].String(), psz ) == 0 )
				return i + 1;
		}
		return 0;
	}
	virtual const char *GetParm( int nIndex ) const override
	{
		if ( nIndex < 0 || nIndex >= m_Parms.Count() )
			return "";
		return m_Parms[nIndex].String();
	}

private:
	CUtlVector< CUtlString > m_Parms;
};

} // namespace

bool ThreadInMainThread()
{
	return true;
}

// Resolution of the high-resolution clock in "ticks" per second.
int64 g_ClockSpeed = 2500000000LL;
double g_ClockSpeedMicrosecondsMultiplier = 1000000.0 / (double)g_ClockSpeed;
double g_ClockSpeedMillisecondsMultiplier = 1000.0 / (double)g_ClockSpeed;

ICommandLine *CommandLine()
{
	static CCommandLineImpl s_CommandLine;
	return &s_CommandLine;
}