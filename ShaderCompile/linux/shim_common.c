// The per-thread TEB, diagnostics and the string conversions, see shim.h
#define _GNU_SOURCE
#include "shim.h"

#include <asm/prctl.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

bool shim_trace_enabled;

static pthread_once_t trace_once = PTHREAD_ONCE_INIT;
static void trace_init( void )
{
	const char* v = getenv( "D3DCOMPILER_SHIM_TRACE" );
	shim_trace_enabled = v && *v && *v != '0';
}

void shim_trace( const char* fmt, ... )
{
	pthread_once( &trace_once, trace_init );
	if ( !shim_trace_enabled )
		return;
	va_list ap;
	va_start( ap, fmt );
	fprintf( stderr, "[shim] " );
	vfprintf( stderr, fmt, ap );
	fputc( '\n', stderr );
	va_end( ap );
}

void shim_fatal( const char* fmt, ... )
{
	va_list ap;
	va_start( ap, fmt );
	fprintf( stderr, "\nShaderCompile: fatal in the d3dcompiler shim: " );
	vfprintf( stderr, fmt, ap );
	fputc( '\n', stderr );
	va_end( ap );
	abort();
}

// A minimal PEB, zeroed, shared by every thread.
// The DLL never read it in traced runs, it exists so TEB->ProcessEnvironmentBlock is not a null pointer
static uint8_t g_peb[0x7C8] __attribute__( ( aligned( 4096 ) ) );

static __thread shim_teb* t_teb;

shim_teb* shim_current_teb( void )
{
	if ( !t_teb )
		shim_thread_attach();
	return t_teb;
}

// Makes this thread's TEB and points the GS segment base at it.
// glibc keeps thread data in FS, GS is free on x86-64 Linux, and this is what Wine does for its own TEB.
// THE TRAP: the DLL's compiled code reads the TEB through GS directly for some fields, so with GS left at zero
// such a read faults at a tiny address, which is what a crash near 0x30 or 0x68 means
void shim_thread_attach( void )
{
	if ( t_teb )
		return;
	shim_teb* teb = mmap( NULL, sizeof( shim_teb ), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
	if ( teb == MAP_FAILED )
		shim_fatal( "mmap for the TEB failed: %s", strerror( errno ) );
	teb->Self = teb;
	teb->ProcessEnvironmentBlock = g_peb;
	teb->UniqueProcess = (void*)(uintptr_t)getpid();
	teb->UniqueThread = (void*)(uintptr_t)syscall( SYS_gettid );
	// Stack bounds, read by code that checks for stack overflow before deep recursion.
	// Approximated from the current stack pointer, 8 MB below and a page above, a thread's default on Linux
	uintptr_t sp = (uintptr_t)__builtin_frame_address( 0 );
	teb->StackBase = (void*)( ( sp + 0x10000 ) & ~(uintptr_t)0xFFF );
	teb->StackLimit = (void*)( ( sp - 0x7F0000 ) & ~(uintptr_t)0xFFF );
	if ( syscall( SYS_arch_prctl, ARCH_SET_GS, (unsigned long)(uintptr_t)teb ) != 0 )
		shim_fatal( "arch_prctl(ARCH_SET_GS) failed: %s", strerror( errno ) );
	t_teb = teb;
	shim_trace( "thread %lu attached, TEB at %p", (unsigned long)syscall( SYS_gettid ), (void*)teb );
}

size_t shim_wcslen( const WCHAR* s )
{
	size_t n = 0;
	while ( s[n] )
		++n;
	return n;
}

// UTF-16 to UTF-8, srcLen in code units, dstLen in bytes, dst may be null to measure
size_t shim_utf16_to_utf8( const WCHAR* src, size_t srcLen, char* dst, size_t dstLen )
{
	size_t out = 0;
	for ( size_t i = 0; i < srcLen; ++i )
	{
		uint32_t c = src[i];
		if ( c >= 0xD800 && c < 0xDC00 && i + 1 < srcLen && src[i + 1] >= 0xDC00 && src[i + 1] < 0xE000 )
		{
			c = 0x10000 + ( ( c - 0xD800 ) << 10 ) + ( src[i + 1] - 0xDC00 );
			++i;
		}
		uint8_t buf[4];
		size_t n;
		if ( c < 0x80 )
		{
			buf[0] = (uint8_t)c;
			n = 1;
		}
		else if ( c < 0x800 )
		{
			buf[0] = (uint8_t)( 0xC0 | ( c >> 6 ) );
			buf[1] = (uint8_t)( 0x80 | ( c & 0x3F ) );
			n = 2;
		}
		else if ( c < 0x10000 )
		{
			buf[0] = (uint8_t)( 0xE0 | ( c >> 12 ) );
			buf[1] = (uint8_t)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
			buf[2] = (uint8_t)( 0x80 | ( c & 0x3F ) );
			n = 3;
		}
		else
		{
			buf[0] = (uint8_t)( 0xF0 | ( c >> 18 ) );
			buf[1] = (uint8_t)( 0x80 | ( ( c >> 12 ) & 0x3F ) );
			buf[2] = (uint8_t)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
			buf[3] = (uint8_t)( 0x80 | ( c & 0x3F ) );
			n = 4;
		}
		if ( dst )
		{
			if ( out + n > dstLen )
				return 0;
			memcpy( dst + out, buf, n );
		}
		out += n;
	}
	return out;
}

// UTF-8 to UTF-16, srcLen in bytes, dstLen in code units, dst may be null to measure.
// A malformed byte becomes U+FFFD rather than failing, the paths and messages this sees are ASCII anyway
size_t shim_utf8_to_utf16( const char* src, size_t srcLen, WCHAR* dst, size_t dstLen )
{
	size_t out = 0;
	for ( size_t i = 0; i < srcLen; )
	{
		uint8_t b = (uint8_t)src[i];
		uint32_t c;
		size_t n;
		if ( b < 0x80 )
		{
			c = b;
			n = 1;
		}
		else if ( ( b & 0xE0 ) == 0xC0 && i + 1 < srcLen )
		{
			c = ( (uint32_t)( b & 0x1F ) << 6 ) | ( src[i + 1] & 0x3F );
			n = 2;
		}
		else if ( ( b & 0xF0 ) == 0xE0 && i + 2 < srcLen )
		{
			c = ( (uint32_t)( b & 0x0F ) << 12 ) | ( (uint32_t)( src[i + 1] & 0x3F ) << 6 ) | ( src[i + 2] & 0x3F );
			n = 3;
		}
		else if ( ( b & 0xF8 ) == 0xF0 && i + 3 < srcLen )
		{
			c = ( (uint32_t)( b & 0x07 ) << 18 ) | ( (uint32_t)( src[i + 1] & 0x3F ) << 12 ) | ( (uint32_t)( src[i + 2] & 0x3F ) << 6 ) | ( src[i + 3] & 0x3F );
			n = 4;
		}
		else
		{
			c = 0xFFFD;
			n = 1;
		}
		i += n;
		WCHAR units[2];
		size_t k;
		if ( c >= 0x10000 )
		{
			c -= 0x10000;
			units[0] = (WCHAR)( 0xD800 + ( c >> 10 ) );
			units[1] = (WCHAR)( 0xDC00 + ( c & 0x3FF ) );
			k = 2;
		}
		else
		{
			units[0] = (WCHAR)c;
			k = 1;
		}
		if ( dst )
		{
			if ( out + k > dstLen )
				return 0;
			memcpy( dst + out, units, k * sizeof( WCHAR ) );
		}
		out += k;
	}
	return out;
}
