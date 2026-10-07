//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: linux dependant ASM code for CPU capability detection
//
// $Workfile:     $
// $NoKeywords: $
//=============================================================================//

#if !defined( _WIN32 )
// processor_detect.cpp covers Windows; this file is POSIX-only.
#if defined(__x86_64__) || defined(_M_X64)
// x86-64 mandates MMX, SSE and SSE2; no cpuid needed (and the 32-bit
// pushl %ebx sequence below is illegal in 64-bit mode).
bool CheckMMXTechnology(void) { return true; }
bool CheckSSETechnology(void) { return true; }
bool CheckSSE2Technology(void) { return true; }
bool Check3DNowTechnology(void) { return false; }

#elif defined(__i386__)
#define cpuid(in,a,b,c,d)												\
	asm("pushl %%ebx\n\t" "cpuid\n\t" "movl %%ebx,%%esi\n\t" "pop %%ebx": "=a" (a), "=S" (b), "=c" (c), "=d" (d) : "a" (in));

bool CheckMMXTechnology(void)
{
    unsigned long eax,ebx,edx,unused;
    cpuid(1,eax,ebx,unused,edx);

    return edx & 0x800000;
}

bool CheckSSETechnology(void)
{
    unsigned long eax,ebx,edx,unused;
    cpuid(1,eax,ebx,unused,edx);

    return edx & 0x2000000L;
}

bool CheckSSE2Technology(void)
{
    unsigned long eax,ebx,edx,unused;
    cpuid(1,eax,ebx,unused,edx);

    return edx & 0x04000000;
}

bool Check3DNowTechnology(void)
{
    unsigned long eax, unused;
    cpuid(0x80000000,eax,unused,unused,unused);

    if ( eax > 0x80000000L )
    {
     	cpuid(0x80000001,unused,unused,unused,eax);
		return ( eax & 1<<31 );
    }
    return false;
}

#else

bool CheckMMXTechnology(void) { return false; }
bool CheckSSETechnology(void) { return false; }
bool CheckSSE2Technology(void) { return false; }
bool Check3DNowTechnology(void) { return false; }

#endif


#endif // !defined( _WIN32 )