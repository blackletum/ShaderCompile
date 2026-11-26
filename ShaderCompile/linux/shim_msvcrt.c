// msvcrt as d3dcompiler_47.dll uses it, see shim.h for the ground rules.
//
// The DLL links the old msvcrt.dll dynamically, so its CRT start-up, string handling, math and allocation all
// come through here.
// Traced compiles are dominated by memset, memcpy, _finite, the ctype functions and malloc/free,
// with the FP control word, the CRT locks and _initterm at start-up.
// THE TRAP: the C++ exception and SEH entry points are fatal stubs on purpose, see the header of peloader.h,
// a compile that reaches them is one this loader cannot run and must say so instead of continuing
#define _GNU_SOURCE
#include "shim.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <xmmintrin.h>

#define CRT( ret, name ) static MSABI ret crt_##name

typedef void( MSABI* crt_pvfv )( void );
typedef int( MSABI* crt_cmp )( const void*, const void* );
typedef void*( MSABI* crt_pfn_alloc )( size_t );
typedef void( MSABI* crt_pfn_free )( void* );

CRT( void, _initterm )( crt_pvfv* first, crt_pvfv* last )
{
	for ( ; first < last; ++first )
		if ( *first )
			( *first )();
}

// The DLL keeps its own atexit table through the two pointers, a fresh one is the (-1) marker msvcrt uses
CRT( crt_pvfv, __dllonexit )( crt_pvfv fn, crt_pvfv** pbegin, crt_pvfv** pend )
{
	crt_pvfv* begin = *pbegin;
	crt_pvfv* end = *pend;
	if ( begin == (crt_pvfv*)(intptr_t)-1 || !begin )
	{
		begin = end = NULL;
	}
	size_t n = (size_t)( end - begin );
	crt_pvfv* grown = realloc( begin, ( n + 1 ) * sizeof( crt_pvfv ) );
	if ( !grown )
		return NULL;
	grown[n] = fn;
	*pbegin = grown;
	*pend = grown + n + 1;
	return fn;
}

CRT( crt_pvfv, _onexit )( crt_pvfv fn )
{
	return fn;
}

CRT( int, _callnewh )( size_t size )
{
	SHIM_UNUSED( size );
	return 0;
}

// The CRT's own lock table, _lock(n) and _unlock(n) by index, 256 is more than msvcrt itself has
static pthread_mutex_t crt_locks[256];
static pthread_once_t crt_locks_once = PTHREAD_ONCE_INIT;
static void crt_locks_init( void )
{
	pthread_mutexattr_t attr;
	pthread_mutexattr_init( &attr );
	pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
	for ( size_t i = 0; i < sizeof( crt_locks ) / sizeof( crt_locks[0] ); ++i )
		pthread_mutex_init( &crt_locks[i], &attr );
	pthread_mutexattr_destroy( &attr );
}

CRT( void, _lock )( int n )
{
	pthread_once( &crt_locks_once, crt_locks_init );
	if ( n < 0 || (size_t)n >= sizeof( crt_locks ) / sizeof( crt_locks[0] ) )
		shim_fatal( "_lock(%d) out of range", n );
	pthread_mutex_lock( &crt_locks[n] );
}

CRT( void, _unlock )( int n )
{
	if ( n < 0 || (size_t)n >= sizeof( crt_locks ) / sizeof( crt_locks[0] ) )
		shim_fatal( "_unlock(%d) out of range", n );
	pthread_mutex_unlock( &crt_locks[n] );
}

static __thread int t_errno;

CRT( int*, _errno )( void )
{
	return &t_errno;
}

CRT( int64_t, _time64 )( int64_t* t )
{
	int64_t now = (int64_t)time( NULL );
	if ( t )
		*t = now;
	return now;
}

CRT( void, _purecall )( void )
{
	shim_fatal( "pure virtual call inside d3dcompiler_47.dll" );
}

CRT( void, _amsg_exit )( int code )
{
	shim_fatal( "CRT runtime error %d inside d3dcompiler_47.dll", code );
}

CRT( void, terminate )( void )
{
	shim_fatal( "std::terminate inside d3dcompiler_47.dll" );
}

CRT( void, type_info_dtor )( void* self )
{
	SHIM_UNUSED( self );
}

CRT( int, _XcptFilter )( ULONG code, void* pointers )
{
	SHIM_UNUSED( code );
	SHIM_UNUSED( pointers );
	return 0; // EXCEPTION_CONTINUE_SEARCH
}

CRT( void, _CxxThrowException )( void* object, void* throwInfo )
{
	SHIM_UNUSED( object );
	SHIM_UNUSED( throwInfo );
	shim_fatal( "d3dcompiler_47.dll threw a C++ exception, which this loader does not dispatch" );
}

CRT( int, __CxxFrameHandler3 )( void* a, void* b, void* c, void* d )
{
	SHIM_UNUSED( a );
	SHIM_UNUSED( b );
	SHIM_UNUSED( c );
	SHIM_UNUSED( d );
	shim_fatal( "__CxxFrameHandler3 reached, exception dispatch is not implemented" );
}

CRT( int, __C_specific_handler )( void* a, void* b, void* c, void* d )
{
	SHIM_UNUSED( a );
	SHIM_UNUSED( b );
	SHIM_UNUSED( c );
	SHIM_UNUSED( d );
	shim_fatal( "__C_specific_handler reached, exception dispatch is not implemented" );
}

// Undecoration of C++ names is for diagnostics, the decorated name is returned unchanged
CRT( char*, __unDName )( char* out, const char* name, int maxLen, crt_pfn_alloc alloc, crt_pfn_free dealloc, unsigned flags )
{
	SHIM_UNUSED( dealloc );
	SHIM_UNUSED( flags );
	size_t len = strlen( name );
	if ( out )
	{
		if ( maxLen <= 0 )
			return NULL;
		snprintf( out, (size_t)maxLen, "%s", name );
		return out;
	}
	char* copy = alloc( len + 1 );
	if ( copy )
		memcpy( copy, name, len + 1 );
	return copy;
}

CRT( void*, malloc )( size_t n )
{
	return malloc( n ? n : 1 );
}

CRT( void, free )( void* p )
{
	free( p );
}

// The DLL passes null pointers with a zero count, fine for msvcrt and undefined for glibc, so zero returns early
CRT( void*, memset )( void* d, int c, size_t n )
{
	return n ? memset( d, c, n ) : d;
}

CRT( void*, memcpy )( void* d, const void* s, size_t n )
{
	return n ? memcpy( d, s, n ) : d;
}

CRT( void*, memmove )( void* d, const void* s, size_t n )
{
	return n ? memmove( d, s, n ) : d;
}

CRT( int, memcmp )( const void* a, const void* b, size_t n )
{
	return n ? memcmp( a, b, n ) : 0;
}

CRT( int, memcpy_s )( void* d, size_t dn, const void* s, size_t n )
{
	if ( n == 0 )
		return 0;
	if ( !d || !s || n > dn )
	{
		if ( d && dn )
			memset( d, 0, dn );
		return n > dn ? 34 /* ERANGE */ : 22 /* EINVAL */;
	}
	memcpy( d, s, n );
	return 0;
}

CRT( int, _memicmp )( const void* a, const void* b, size_t n )
{
	const unsigned char* x = a;
	const unsigned char* y = b;
	for ( size_t i = 0; i < n; ++i )
	{
		int cx = tolower( x[i] ), cy = tolower( y[i] );
		if ( cx != cy )
			return cx < cy ? -1 : 1;
	}
	return 0;
}

// ctype in the C locale, ASCII only.
// Out-of-range arguments are undefined behavior in MSVC's tables too, the DLL only passes chars
CRT( int, isalnum )( int c ) { return c >= 0 && c < 128 && isalnum( c ); }
CRT( int, isalpha )( int c ) { return c >= 0 && c < 128 && isalpha( c ); }
CRT( int, isdigit )( int c ) { return c >= '0' && c <= '9'; }
CRT( int, isspace )( int c ) { return c >= 0 && c < 128 && isspace( c ); }
CRT( int, isxdigit )( int c ) { return c >= 0 && c < 128 && isxdigit( c ); }
CRT( int, __isascii )( int c ) { return (unsigned)c < 128; }
CRT( int, tolower )( int c ) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
CRT( int, toupper )( int c ) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
CRT( WCHAR, towlower )( WCHAR c ) { return c >= 'A' && c <= 'Z' ? (WCHAR)( c + 32 ) : c; }

CRT( size_t, strnlen )( const char* s, size_t n ) { return strnlen( s, n ); }
CRT( int, strcmp )( const char* a, const char* b ) { return strcmp( a, b ); }
CRT( int, strncmp )( const char* a, const char* b, size_t n ) { return strncmp( a, b, n ); }
CRT( char*, strchr )( const char* s, int c ) { return (char*)strchr( s, c ); }
CRT( char*, strrchr )( const char* s, int c ) { return (char*)strrchr( s, c ); }
CRT( char*, strstr )( const char* s, const char* n ) { return (char*)strstr( s, n ); }
CRT( int, _stricmp )( const char* a, const char* b ) { return strcasecmp( a, b ); }
CRT( int, _strnicmp )( const char* a, const char* b, size_t n ) { return strncasecmp( a, b, n ); }
CRT( int, _mbscmp )( const unsigned char* a, const unsigned char* b ) { return strcmp( (const char*)a, (const char*)b ); }
CRT( size_t, _mbstrlen )( const char* s ) { return strlen( s ); }
CRT( char*, _strdup )( const char* s ) { return s ? strdup( s ) : NULL; }

CRT( int, strcpy_s )( char* d, size_t dn, const char* s )
{
	if ( !d || !dn )
		return 22;
	if ( !s )
	{
		d[0] = 0;
		return 22;
	}
	size_t n = strlen( s );
	if ( n >= dn )
	{
		d[0] = 0;
		return 34;
	}
	memcpy( d, s, n + 1 );
	return 0;
}

CRT( int, strncpy_s )( char* d, size_t dn, const char* s, size_t count )
{
	if ( !d || !dn )
		return 22;
	if ( !s )
	{
		d[0] = 0;
		return 22;
	}
	size_t n = strnlen( s, count == (size_t)-1 ? dn : count );
	if ( n >= dn )
	{
		if ( count == (size_t)-1 )
		{
			// _TRUNCATE
			memcpy( d, s, dn - 1 );
			d[dn - 1] = 0;
			return 80; // STRUNCATE
		}
		d[0] = 0;
		return 34;
	}
	memcpy( d, s, n );
	d[n] = 0;
	return 0;
}

CRT( int, strcat_s )( char* d, size_t dn, const char* s )
{
	if ( !d || !dn )
		return 22;
	size_t have = strnlen( d, dn );
	if ( have == dn )
		return 22;
	size_t n = strlen( s );
	if ( have + n >= dn )
	{
		d[0] = 0;
		return 34;
	}
	memcpy( d + have, s, n + 1 );
	return 0;
}

// The wide functions are written out rather than passed to glibc, a WCHAR is 16 bits and glibc's wchar_t is 32
CRT( size_t, wcslen )( const WCHAR* s ) { return shim_wcslen( s ); }

CRT( int, wcsncmp )( const WCHAR* a, const WCHAR* b, size_t n )
{
	for ( size_t i = 0; i < n; ++i )
	{
		if ( a[i] != b[i] )
			return a[i] < b[i] ? -1 : 1;
		if ( !a[i] )
			return 0;
	}
	return 0;
}

CRT( int, _wcsicmp )( const WCHAR* a, const WCHAR* b )
{
	for ( ;; ++a, ++b )
	{
		WCHAR x = crt_towlower( *a ), y = crt_towlower( *b );
		if ( x != y )
			return x < y ? -1 : 1;
		if ( !x )
			return 0;
	}
}

CRT( int, _wcsnicmp )( const WCHAR* a, const WCHAR* b, size_t n )
{
	for ( size_t i = 0; i < n; ++i )
	{
		WCHAR x = crt_towlower( a[i] ), y = crt_towlower( b[i] );
		if ( x != y )
			return x < y ? -1 : 1;
		if ( !x )
			return 0;
	}
	return 0;
}

CRT( WCHAR*, wcsrchr )( const WCHAR* s, WCHAR c )
{
	const WCHAR* last = NULL;
	for ( ; *s; ++s )
		if ( *s == c )
			last = s;
	if ( c == 0 )
		return (WCHAR*)s;
	return (WCHAR*)last;
}

CRT( WCHAR*, _wcsdup )( const WCHAR* s )
{
	if ( !s )
		return NULL;
	size_t n = shim_wcslen( s ) + 1;
	WCHAR* d = malloc( n * sizeof( WCHAR ) );
	if ( d )
		memcpy( d, s, n * sizeof( WCHAR ) );
	return d;
}

CRT( int, wcscpy_s )( WCHAR* d, size_t dn, const WCHAR* s )
{
	if ( !d || !dn )
		return 22;
	if ( !s )
	{
		d[0] = 0;
		return 22;
	}
	size_t n = shim_wcslen( s );
	if ( n >= dn )
	{
		d[0] = 0;
		return 34;
	}
	memcpy( d, s, ( n + 1 ) * sizeof( WCHAR ) );
	return 0;
}

CRT( int, wcsncpy_s )( WCHAR* d, size_t dn, const WCHAR* s, size_t count )
{
	if ( !d || !dn )
		return 22;
	if ( !s )
	{
		d[0] = 0;
		return 22;
	}
	size_t n = 0;
	size_t limit = count == (size_t)-1 ? dn : count;
	while ( n < limit && s[n] )
		++n;
	if ( n >= dn )
	{
		if ( count == (size_t)-1 )
		{
			memcpy( d, s, ( dn - 1 ) * sizeof( WCHAR ) );
			d[dn - 1] = 0;
			return 80;
		}
		d[0] = 0;
		return 34;
	}
	memcpy( d, s, n * sizeof( WCHAR ) );
	d[n] = 0;
	return 0;
}

CRT( int, wcscat_s )( WCHAR* d, size_t dn, const WCHAR* s )
{
	if ( !d || !dn )
		return 22;
	size_t have = 0;
	while ( have < dn && d[have] )
		++have;
	if ( have == dn )
		return 22;
	size_t n = shim_wcslen( s );
	if ( have + n >= dn )
	{
		d[0] = 0;
		return 34;
	}
	memcpy( d + have, s, ( n + 1 ) * sizeof( WCHAR ) );
	return 0;
}

CRT( int, wcsncat_s )( WCHAR* d, size_t dn, const WCHAR* s, size_t count )
{
	if ( !d || !dn )
		return 22;
	size_t have = 0;
	while ( have < dn && d[have] )
		++have;
	if ( have == dn )
		return 22;
	size_t n = 0;
	while ( n < count && s[n] )
		++n;
	if ( have + n >= dn )
	{
		d[0] = 0;
		return 34;
	}
	memcpy( d + have, s, n * sizeof( WCHAR ) );
	d[have + n] = 0;
	return 0;
}

CRT( int, atoi )( const char* s ) { return atoi( s ); }
CRT( int64_t, _atoi64 )( const char* s ) { return strtoll( s, NULL, 10 ); }
CRT( double, atof )( const char* s ) { return strtod( s, NULL ); }
CRT( double, strtod )( const char* s, char** end ) { return strtod( s, end ); }
CRT( unsigned long, strtoul )( const char* s, char** end, int base ) { return (uint32_t)strtoul( s, end, base ); }
CRT( uint64_t, _strtoui64 )( const char* s, char** end, int base ) { return strtoull( s, end, base ); }

CRT( uint32_t, wcstoul )( const WCHAR* s, WCHAR** end, int base )
{
	// Narrow the digits, parse, map the end pointer back
	char buf[128];
	size_t n = 0;
	while ( s[n] && s[n] < 128 && n < sizeof( buf ) - 1 )
	{
		buf[n] = (char)s[n];
		++n;
	}
	buf[n] = 0;
	char* e = buf;
	unsigned long v = strtoul( buf, &e, base );
	if ( end )
		*end = (WCHAR*)( s + ( e - buf ) );
	return (uint32_t)v;
}

// The comparator is the DLL's and so MSABI, which glibc's qsort cannot call, hence qsort_r through this adapter
static int cmp_adapter( const void* a, const void* b, void* ctx )
{
	return ( (crt_cmp)ctx )( a, b );
}

CRT( void, qsort )( void* base, size_t n, size_t size, crt_cmp cmp )
{
	shim_trace( "qsort(%zu elements of %zu)", n, size );
	qsort_r( base, n, size, cmp_adapter, (void*)cmp );
}

CRT( void*, bsearch )( const void* key, const void* base, size_t n, size_t size, crt_cmp cmp )
{
	size_t lo = 0, hi = n;
	while ( lo < hi )
	{
		size_t mid = lo + ( hi - lo ) / 2;
		const char* p = (const char*)base + mid * size;
		int r = cmp( key, p );
		if ( r == 0 )
			return (void*)p;
		if ( r < 0 )
			hi = mid;
		else
			lo = mid + 1;
	}
	return NULL;
}

CRT( double, acos )( double x ) { return acos( x ); }
CRT( double, asin )( double x ) { return asin( x ); }
CRT( double, atan )( double x ) { return atan( x ); }
CRT( double, atan2 )( double y, double x ) { return atan2( y, x ); }
CRT( double, ceil )( double x ) { return ceil( x ); }
CRT( double, cos )( double x ) { return cos( x ); }
CRT( double, cosh )( double x ) { return cosh( x ); }
CRT( double, exp )( double x ) { return exp( x ); }
CRT( double, floor )( double x ) { return floor( x ); }
CRT( float, floorf )( float x ) { return floorf( x ); }
CRT( double, fmod )( double x, double y ) { return fmod( x, y ); }
CRT( double, log )( double x ) { return log( x ); }
CRT( double, modf )( double x, double* ip ) { return modf( x, ip ); }
CRT( double, pow )( double x, double y ) { return pow( x, y ); }
CRT( double, sin )( double x ) { return sin( x ); }
CRT( double, sinh )( double x ) { return sinh( x ); }
CRT( double, sqrt )( double x ) { return sqrt( x ); }
CRT( double, tan )( double x ) { return tan( x ); }
CRT( double, tanh )( double x ) { return tanh( x ); }
CRT( int, _finite )( double x ) { return isfinite( x ) ? 1 : 0; }
CRT( int, _isnan )( double x ) { return isnan( x ) ? 1 : 0; }

// MSVC's classes
#define FPCLASS_SNAN 0x0001
#define FPCLASS_QNAN 0x0002
#define FPCLASS_NINF 0x0004
#define FPCLASS_NN 0x0008
#define FPCLASS_ND 0x0010
#define FPCLASS_NZ 0x0020
#define FPCLASS_PZ 0x0040
#define FPCLASS_PD 0x0080
#define FPCLASS_PN 0x0100
#define FPCLASS_PINF 0x0200

CRT( int, _fpclass )( double x )
{
	union { double d; uint64_t u; } v = { x };
	int neg = ( v.u >> 63 ) != 0;
	switch ( fpclassify( x ) )
	{
	case FP_NAN: return ( v.u & 0x0008000000000000ull ) ? FPCLASS_QNAN : FPCLASS_SNAN;
	case FP_INFINITE: return neg ? FPCLASS_NINF : FPCLASS_PINF;
	case FP_ZERO: return neg ? FPCLASS_NZ : FPCLASS_PZ;
	case FP_SUBNORMAL: return neg ? FPCLASS_ND : FPCLASS_PD;
	default: return neg ? FPCLASS_NN : FPCLASS_PN;
	}
}

// The MSVC control word against MXCSR.
// Exception masks, rounding and denormal handling are the fields with an MXCSR equivalent,
// precision control has none on x64 and reads back as 64-bit, which is what MSVC reports there too
#define MCW_EM 0x0008001Fu
#define EM_INEXACT 0x01u
#define EM_UNDERFLOW 0x02u
#define EM_OVERFLOW 0x04u
#define EM_ZERODIVIDE 0x08u
#define EM_INVALID 0x10u
#define EM_DENORMAL 0x80000u
#define MCW_RC 0x300u
#define MCW_DN 0x3000000u
#define DN_FLUSH 0x1000000u

static unsigned mxcsr_to_cw( unsigned m )
{
	unsigned cw = 0;
	if ( m & ( 1u << 7 ) ) cw |= EM_INVALID;
	if ( m & ( 1u << 8 ) ) cw |= EM_DENORMAL;
	if ( m & ( 1u << 9 ) ) cw |= EM_ZERODIVIDE;
	if ( m & ( 1u << 10 ) ) cw |= EM_OVERFLOW;
	if ( m & ( 1u << 11 ) ) cw |= EM_UNDERFLOW;
	if ( m & ( 1u << 12 ) ) cw |= EM_INEXACT;
	cw |= ( ( m >> 13 ) & 3u ) << 8; // rounding, same encoding order
	if ( m & ( 1u << 15 ) ) cw |= DN_FLUSH;
	return cw;
}

static unsigned cw_to_mxcsr( unsigned cw, unsigned m )
{
	m &= ~( 0x3Fu << 7 | 3u << 13 | 1u << 15 | 1u << 6 );
	if ( cw & EM_INVALID ) m |= 1u << 7;
	if ( cw & EM_DENORMAL ) m |= 1u << 8;
	if ( cw & EM_ZERODIVIDE ) m |= 1u << 9;
	if ( cw & EM_OVERFLOW ) m |= 1u << 10;
	if ( cw & EM_UNDERFLOW ) m |= 1u << 11;
	if ( cw & EM_INEXACT ) m |= 1u << 12;
	m |= ( ( cw >> 8 ) & 3u ) << 13;
	if ( cw & DN_FLUSH ) m |= 1u << 15 | 1u << 6;
	return m;
}

CRT( unsigned, _controlfp )( unsigned newcw, unsigned mask )
{
	unsigned m = _mm_getcsr();
	unsigned cw = mxcsr_to_cw( m );
	if ( mask )
	{
		cw = ( cw & ~mask ) | ( newcw & mask );
		_mm_setcsr( cw_to_mxcsr( cw, m ) );
	}
	return cw;
}

CRT( unsigned, _clearfp )( void )
{
	unsigned m = _mm_getcsr();
	unsigned sw = 0;
	if ( m & 1u ) sw |= EM_INVALID;
	if ( m & 2u ) sw |= EM_DENORMAL;
	if ( m & 4u ) sw |= EM_ZERODIVIDE;
	if ( m & 8u ) sw |= EM_OVERFLOW;
	if ( m & 16u ) sw |= EM_UNDERFLOW;
	if ( m & 32u ) sw |= EM_INEXACT;
	_mm_setcsr( m & ~0x3Fu );
	return sw;
}

CRT( char*, getenv )( const char* name )
{
	return getenv( name );
}

static __thread WCHAR t_wenv[1024];

CRT( WCHAR*, _wgetenv )( const WCHAR* name )
{
	char narrow[256];
	size_t n = shim_utf16_to_utf8( name, shim_wcslen( name ), narrow, sizeof( narrow ) - 1 );
	narrow[n] = 0;
	const char* v = getenv( narrow );
	if ( !v )
		return NULL;
	size_t w = shim_utf8_to_utf16( v, strlen( v ), t_wenv, sizeof( t_wenv ) / sizeof( WCHAR ) - 1 );
	t_wenv[w] = 0;
	return t_wenv;
}

// The C locale is the only one, queries and requests alike answer "C"
CRT( char*, setlocale )( int category, const char* locale )
{
	SHIM_UNUSED( category );
	SHIM_UNUSED( locale );
	static char c_locale[] = "C";
	return c_locale;
}

// The CRT's file descriptors are the POSIX ones as they are, only the open flags need translating
#define O_MS_WRONLY 0x0001
#define O_MS_RDWR 0x0002
#define O_MS_APPEND 0x0008
#define O_MS_CREAT 0x0100
#define O_MS_TRUNC 0x0200
#define O_MS_EXCL 0x0400

static int ms_oflag_to_posix( int o )
{
	int f = ( o & O_MS_RDWR ) ? O_RDWR : ( o & O_MS_WRONLY ) ? O_WRONLY : O_RDONLY;
	if ( o & O_MS_APPEND ) f |= O_APPEND;
	if ( o & O_MS_CREAT ) f |= O_CREAT;
	if ( o & O_MS_TRUNC ) f |= O_TRUNC;
	if ( o & O_MS_EXCL ) f |= O_EXCL;
	return f | O_CLOEXEC;
}

static void wide_path( const WCHAR* w, char* out, size_t outLen )
{
	char tmp[PATH_MAX];
	size_t n = shim_utf16_to_utf8( w, shim_wcslen( w ), tmp, sizeof( tmp ) - 1 );
	tmp[n] = 0;
	const char* in = tmp;
	if ( ( ( in[0] >= 'A' && in[0] <= 'Z' ) || ( in[0] >= 'a' && in[0] <= 'z' ) ) && in[1] == ':' )
		in += 2;
	size_t j = 0;
	for ( ; *in && j + 1 < outLen; ++in )
		out[j++] = *in == '\\' ? '/' : *in;
	out[j] = 0;
}

CRT( int, _wsopen )( const WCHAR* name, int oflag, int shflag, int pmode )
{
	SHIM_UNUSED( shflag );
	SHIM_UNUSED( pmode );
	char path[PATH_MAX];
	wide_path( name, path, sizeof( path ) );
	int fd = open( path, ms_oflag_to_posix( oflag ), 0644 );
	if ( fd < 0 )
		t_errno = errno;
	return fd;
}

CRT( FILE*, _wfsopen )( const WCHAR* name, const WCHAR* mode, int shflag )
{
	SHIM_UNUSED( shflag );
	char path[PATH_MAX], m[16];
	wide_path( name, path, sizeof( path ) );
	size_t n = 0;
	for ( ; mode[n] && n < sizeof( m ) - 1; ++n )
		m[n] = (char)mode[n]; // t and b and ccs= are ignored by glibc, which is fine, bytes are bytes
	m[n] = 0;
	FILE* f = fopen( path, m );
	if ( !f )
		t_errno = errno;
	return f;
}

CRT( int, fclose )( FILE* f ) { return fclose( f ); }
CRT( size_t, fread )( void* p, size_t sz, size_t n, FILE* f ) { return fread( p, sz, n, f ); }
CRT( int, fseek )( FILE* f, long off, int whence ) { return fseek( f, off, whence ); }
CRT( long, ftell )( FILE* f ) { return (long)ftell( f ); }
CRT( int, _fileno )( FILE* f ) { return fileno( f ); }
CRT( int, _close )( int fd ) { return close( fd ); }
CRT( int, _read )( int fd, void* buf, unsigned n ) { return (int)read( fd, buf, n ); }
CRT( int, _write )( int fd, const void* buf, unsigned n ) { return (int)write( fd, buf, n ); }
CRT( int64_t, _lseeki64 )( int fd, int64_t off, int whence ) { return lseek( fd, off, whence ); }

CRT( int64_t, _filelengthi64 )( int fd )
{
	struct stat st;
	return fstat( fd, &st ) ? -1 : st.st_size;
}

CRT( int, _chsize_s )( int fd, int64_t size )
{
	return ftruncate( fd, size ) ? errno : 0;
}

CRT( intptr_t, _get_osfhandle )( int fd )
{
	return fd < 0 ? -1 : fd + SHIM_HANDLE_BIAS;
}

CRT( int, _open_osfhandle )( intptr_t handle, int flags )
{
	SHIM_UNUSED( flags );
	return handle >= SHIM_HANDLE_BIAS ? (int)( handle - SHIM_HANDLE_BIAS ) : -1;
}

CRT( WCHAR*, _wfullpath )( WCHAR* out, const WCHAR* path, size_t maxLen )
{
	char narrow[PATH_MAX], full[PATH_MAX * 2];
	wide_path( path, narrow, sizeof( narrow ) );
	if ( narrow[0] == '/' )
		snprintf( full, sizeof( full ), "%s", narrow );
	else
	{
		char cwd[PATH_MAX];
		if ( !getcwd( cwd, sizeof( cwd ) ) )
			cwd[0] = 0;
		snprintf( full, sizeof( full ), "%s/%s", cwd, narrow );
	}
	size_t need = shim_utf8_to_utf16( full, strlen( full ), NULL, 0 ) + 1;
	if ( !out )
	{
		out = malloc( need * sizeof( WCHAR ) );
		maxLen = need;
	}
	if ( maxLen < need )
		return NULL;
	shim_utf8_to_utf16( full, strlen( full ), out, maxLen );
	out[need - 1] = 0;
	return out;
}

static size_t wcat( WCHAR* d, size_t dn, size_t at, const WCHAR* s )
{
	for ( ; s && *s && at + 1 < dn; ++s )
		d[at++] = *s;
	return at;
}

CRT( int, _wmakepath_s )( WCHAR* path, size_t n, const WCHAR* drive, const WCHAR* dir, const WCHAR* fname, const WCHAR* ext )
{
	size_t at = 0;
	if ( drive && *drive )
	{
		path[at++] = drive[0];
		if ( at + 1 < n )
			path[at++] = ':';
	}
	at = wcat( path, n, at, dir );
	if ( dir && *dir && at && path[at - 1] != '\\' && path[at - 1] != '/' && at + 1 < n )
		path[at++] = '\\';
	at = wcat( path, n, at, fname );
	if ( ext && *ext )
	{
		if ( ext[0] != '.' && at + 1 < n )
			path[at++] = '.';
		at = wcat( path, n, at, ext );
	}
	if ( at >= n )
	{
		path[0] = 0;
		return 34;
	}
	path[at] = 0;
	return 0;
}

static int wcopy_part( WCHAR* d, size_t dn, const WCHAR* s, size_t len )
{
	if ( !d )
		return dn ? 22 : 0;
	if ( len >= dn )
	{
		d[0] = 0;
		return 34;
	}
	memcpy( d, s, len * sizeof( WCHAR ) );
	d[len] = 0;
	return 0;
}

CRT( int, _wsplitpath_s )( const WCHAR* path, WCHAR* drive, size_t dn, WCHAR* dir, size_t dirn, WCHAR* fname, size_t fn, WCHAR* ext, size_t en )
{
	const WCHAR* p = path;
	const WCHAR* driveEnd = p;
	if ( p[0] && p[1] == ':' )
		driveEnd = p + 2;
	const WCHAR* lastSep = NULL;
	for ( const WCHAR* q = driveEnd; *q; ++q )
		if ( *q == '\\' || *q == '/' )
			lastSep = q;
	const WCHAR* dirEnd = lastSep ? lastSep + 1 : driveEnd;
	const WCHAR* dot = NULL;
	for ( const WCHAR* q = dirEnd; *q; ++q )
		if ( *q == '.' )
			dot = q;
	const WCHAR* nameEnd = dot ? dot : dirEnd + shim_wcslen( dirEnd );
	int r = 0;
	r |= wcopy_part( drive, dn, path, (size_t)( driveEnd - path ) );
	r |= wcopy_part( dir, dirn, driveEnd, (size_t)( dirEnd - driveEnd ) );
	r |= wcopy_part( fname, fn, dirEnd, (size_t)( nameEnd - dirEnd ) );
	r |= wcopy_part( ext, en, nameEnd, shim_wcslen( nameEnd ) );
	return r;
}

typedef struct fmt_out
{
	char* buf;
	size_t cap;
	size_t len; // characters that would have been written
} fmt_out;

static void out_bytes( fmt_out* o, const char* s, size_t n )
{
	for ( size_t i = 0; i < n; ++i )
	{
		if ( o->len + 1 < o->cap )
			o->buf[o->len] = s[i];
		o->len++;
	}
}

static void out_wide_as_utf8( fmt_out* o, const WCHAR* s, size_t n )
{
	char tmp[8];
	for ( size_t i = 0; i < n; ++i )
	{
		size_t k = shim_utf16_to_utf8( s + i, 1, tmp, sizeof( tmp ) );
		out_bytes( o, tmp, k );
	}
}

static void pad( fmt_out* o, size_t count )
{
	for ( size_t i = 0; i < count; ++i )
		out_bytes( o, " ", 1 );
}

// printf over the DLL's argument list.
// A va_list from the DLL is a pointer into its Win64 argument area, which GCC exposes as __builtin_ms_va_list,
// so the format string is walked here and every conversion is handed to snprintf one at a time.
// Covers what a compiler's messages use, the integer, pointer, string, character and float conversions
// with flags, width and precision, in both the wide and the narrow functions.
// MSVC's %s is a narrow string in the narrow functions and a wide one in the wide functions, %S the other,
// %ls, %ws and %hs say so explicitly, and wideDefault is which function family we are in
static void format_ms( fmt_out* o, const char* fmt, __builtin_ms_va_list* ap, bool wideDefault )
{
	while ( *fmt )
	{
		if ( *fmt != '%' )
		{
			out_bytes( o, fmt, 1 );
			++fmt;
			continue;
		}
		const char* start = fmt++;
		if ( *fmt == '%' )
		{
			out_bytes( o, "%", 1 );
			++fmt;
			continue;
		}
		char flags[8] = { 0 };
		size_t nf = 0;
		while ( strchr( "-+ #0", *fmt ) && *fmt && nf < sizeof( flags ) - 1 )
			flags[nf++] = *fmt++;
		int width = -1, prec = -1;
		if ( *fmt == '*' )
		{
			width = __builtin_va_arg( *ap, int );
			++fmt;
		}
		else if ( isdigit( (unsigned char)*fmt ) )
		{
			width = 0;
			while ( isdigit( (unsigned char)*fmt ) )
				width = width * 10 + ( *fmt++ - '0' );
		}
		if ( *fmt == '.' )
		{
			++fmt;
			if ( *fmt == '*' )
			{
				prec = __builtin_va_arg( *ap, int );
				++fmt;
			}
			else
			{
				prec = 0;
				while ( isdigit( (unsigned char)*fmt ) )
					prec = prec * 10 + ( *fmt++ - '0' );
			}
		}
		// Length modifiers, I64 and I32 and I are MSVC's, w is a wide string
		int bits = 32;
		int wideness = 0; // 1 wide, -1 narrow, 0 by default
		for ( ;; )
		{
			if ( fmt[0] == 'I' && fmt[1] == '6' && fmt[2] == '4' ) { bits = 64; fmt += 3; }
			else if ( fmt[0] == 'I' && fmt[1] == '3' && fmt[2] == '2' ) { bits = 32; fmt += 3; }
			else if ( fmt[0] == 'I' || fmt[0] == 'z' || fmt[0] == 't' || fmt[0] == 'j' ) { bits = 64; fmt += 1; }
			else if ( fmt[0] == 'l' && fmt[1] == 'l' ) { bits = 64; fmt += 2; }
			else if ( fmt[0] == 'l' ) { wideness = 1; fmt += 1; } // long is 32 bits, but %ls is wide
			else if ( fmt[0] == 'w' ) { wideness = 1; fmt += 1; }
			else if ( fmt[0] == 'h' ) { wideness = -1; fmt += 1; }
			else if ( fmt[0] == 'L' ) { fmt += 1; }
			else break;
		}
		char conv = *fmt++;
		char spec[64];
		char tmp[512];
		int wlen;
		switch ( conv )
		{
		case 'd':
		case 'i':
		case 'u':
		case 'x':
		case 'X':
		case 'o':
		{
			long long v = bits == 64 ? __builtin_va_arg( *ap, long long ) : ( conv == 'd' || conv == 'i' ) ? (long long)__builtin_va_arg( *ap, int ) : (long long)__builtin_va_arg( *ap, unsigned );
			snprintf( spec, sizeof( spec ), "%%%s%s%sll%c", flags, width >= 0 ? "*" : "", prec >= 0 ? ".*" : "", conv );
			if ( width >= 0 && prec >= 0 ) wlen = snprintf( tmp, sizeof( tmp ), spec, width, prec, v );
			else if ( width >= 0 ) wlen = snprintf( tmp, sizeof( tmp ), spec, width, v );
			else if ( prec >= 0 ) wlen = snprintf( tmp, sizeof( tmp ), spec, prec, v );
			else wlen = snprintf( tmp, sizeof( tmp ), spec, v );
			out_bytes( o, tmp, (size_t)wlen );
			break;
		}
		case 'p':
		{
			void* v = __builtin_va_arg( *ap, void* );
			wlen = snprintf( tmp, sizeof( tmp ), "%016llX", (unsigned long long)(uintptr_t)v );
			out_bytes( o, tmp, (size_t)wlen );
			break;
		}
		case 'f':
		case 'F':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
		case 'a':
		case 'A':
		{
			double v = __builtin_va_arg( *ap, double );
			snprintf( spec, sizeof( spec ), "%%%s%s%s%c", flags, width >= 0 ? "*" : "", prec >= 0 ? ".*" : "", conv );
			if ( width >= 0 && prec >= 0 ) wlen = snprintf( tmp, sizeof( tmp ), spec, width, prec, v );
			else if ( width >= 0 ) wlen = snprintf( tmp, sizeof( tmp ), spec, width, v );
			else if ( prec >= 0 ) wlen = snprintf( tmp, sizeof( tmp ), spec, prec, v );
			else wlen = snprintf( tmp, sizeof( tmp ), spec, v );
			out_bytes( o, tmp, (size_t)wlen );
			break;
		}
		case 'c':
		case 'C':
		{
			bool wide = wideness == 1 || ( wideness == 0 && ( conv == 'C' ) != wideDefault );
			int v = __builtin_va_arg( *ap, int );
			if ( width > 1 && !strchr( flags, '-' ) ) pad( o, (size_t)width - 1 );
			if ( wide )
			{
				WCHAR w = (WCHAR)v;
				out_wide_as_utf8( o, &w, 1 );
			}
			else
				out_bytes( o, (char[]){ (char)v }, 1 );
			if ( width > 1 && strchr( flags, '-' ) ) pad( o, (size_t)width - 1 );
			break;
		}
		case 's':
		case 'S':
		{
			bool wide = wideness == 1 || ( wideness == 0 && ( conv == 'S' ) != wideDefault );
			const void* v = __builtin_va_arg( *ap, const void* );
			size_t n;
			if ( wide )
			{
				const WCHAR* ws = v ? v : (const WCHAR[]){ '(', 'n', 'u', 'l', 'l', ')', 0 };
				n = shim_wcslen( ws );
				if ( prec >= 0 && (size_t)prec < n ) n = (size_t)prec;
				if ( width > (int)n && !strchr( flags, '-' ) ) pad( o, (size_t)width - n );
				out_wide_as_utf8( o, ws, n );
				if ( width > (int)n && strchr( flags, '-' ) ) pad( o, (size_t)width - n );
			}
			else
			{
				const char* s = v ? v : "(null)";
				n = prec >= 0 ? strnlen( s, (size_t)prec ) : strlen( s );
				if ( width > (int)n && !strchr( flags, '-' ) ) pad( o, (size_t)width - n );
				out_bytes( o, s, n );
				if ( width > (int)n && strchr( flags, '-' ) ) pad( o, (size_t)width - n );
			}
			break;
		}
		default:
			shim_fatal( "printf conversion \"%.*s\" not implemented", (int)( fmt - start ), start );
		}
	}
}

// snprintf semantics, the count of characters that would have been written, buffer always terminated when cap > 0
static int format_narrow( char* buf, size_t cap, const char* fmt, __builtin_ms_va_list* ap, bool wideDefault )
{
	fmt_out o = { buf, cap, 0 };
	format_ms( &o, fmt, ap, wideDefault );
	if ( cap )
		buf[o.len < cap ? o.len : cap - 1] = 0;
	return (int)o.len;
}

static void narrow_fmt( const WCHAR* wfmt, char* out, size_t outLen )
{
	size_t n = shim_utf16_to_utf8( wfmt, shim_wcslen( wfmt ), out, outLen - 1 );
	out[n] = 0;
}

// Wide output goes through a UTF-8 scratch buffer and back, the scratch is sized for messages, not documents
static int format_wide( WCHAR* buf, size_t cap, const WCHAR* wfmt, __builtin_ms_va_list* ap )
{
	char fmt[1024];
	narrow_fmt( wfmt, fmt, sizeof( fmt ) );
	char tmp[8192];
	fmt_out o = { tmp, sizeof( tmp ), 0 };
	format_ms( &o, fmt, ap, true );
	size_t n = o.len < sizeof( tmp ) ? o.len : sizeof( tmp ) - 1;
	tmp[n] = 0;
	size_t need = shim_utf8_to_utf16( tmp, n, NULL, 0 );
	if ( cap )
	{
		size_t w = need < cap ? need : cap - 1;
		shim_utf8_to_utf16( tmp, n, buf, cap );
		buf[w] = 0;
	}
	return (int)need;
}

CRT( int, sprintf_s )( char* buf, size_t cap, const char* fmt, ... )
{
	__builtin_ms_va_list ap;
	__builtin_ms_va_start( ap, fmt );
	int n = format_narrow( buf, cap, fmt, &ap, false );
	__builtin_ms_va_end( ap );
	if ( (size_t)n >= cap )
	{
		if ( cap )
			buf[0] = 0;
		return -1;
	}
	return n;
}

CRT( int, _vsnprintf )( char* buf, size_t count, const char* fmt, __builtin_ms_va_list ap )
{
	// Fills exactly count bytes without a terminator when the output does not fit, as msvcrt does
	char tmp[8192];
	int n = format_narrow( tmp, sizeof( tmp ), fmt, &ap, false );
	if ( (size_t)n >= count )
	{
		if ( buf && count )
			memcpy( buf, tmp, count );
		return -1;
	}
	if ( buf )
		memcpy( buf, tmp, (size_t)n + 1 );
	return n;
}

CRT( int, _vsnwprintf )( WCHAR* buf, size_t count, const WCHAR* fmt, __builtin_ms_va_list ap )
{
	WCHAR tmp[8192];
	int n = format_wide( tmp, sizeof( tmp ) / sizeof( WCHAR ), fmt, &ap );
	if ( (size_t)n >= count )
	{
		if ( buf && count )
			memcpy( buf, tmp, count * sizeof( WCHAR ) );
		return -1;
	}
	if ( buf )
		memcpy( buf, tmp, ( (size_t)n + 1 ) * sizeof( WCHAR ) );
	return n;
}

CRT( int, swprintf_s )( WCHAR* buf, size_t cap, const WCHAR* fmt, ... )
{
	__builtin_ms_va_list ap;
	__builtin_ms_va_start( ap, fmt );
	int n = format_wide( buf, cap, fmt, &ap );
	__builtin_ms_va_end( ap );
	if ( (size_t)n >= cap )
	{
		if ( cap )
			buf[0] = 0;
		return -1;
	}
	return n;
}

CRT( int, _snwprintf_s )( WCHAR* buf, size_t cap, size_t count, const WCHAR* fmt, ... )
{
	__builtin_ms_va_list ap;
	__builtin_ms_va_start( ap, fmt );
	WCHAR tmp[8192];
	int n = format_wide( tmp, sizeof( tmp ) / sizeof( WCHAR ), fmt, &ap );
	__builtin_ms_va_end( ap );
	size_t limit = count == (size_t)-1 ? cap - 1 : count; // _TRUNCATE
	if ( (size_t)n > limit || (size_t)n >= cap )
	{
		if ( count == (size_t)-1 && cap )
		{
			memcpy( buf, tmp, ( cap - 1 ) * sizeof( WCHAR ) );
			buf[cap - 1] = 0;
		}
		else if ( cap )
			buf[0] = 0;
		return -1;
	}
	memcpy( buf, tmp, ( (size_t)n + 1 ) * sizeof( WCHAR ) );
	return n;
}

// Reading formatted input is not something a compile does, say so loudly if it ever is
CRT( int, sscanf_s )( const char* s, const char* fmt, ... )
{
	shim_fatal( "sscanf_s(\"%.40s\", \"%s\") is not implemented", s, fmt );
}

const shim_entry shim_msvcrt_table[] = {
	{ "msvcrt.dll", "??1type_info@@UEAA@XZ", (void*)crt_type_info_dtor },
	{ "msvcrt.dll", "?terminate@@YAXXZ", (void*)crt_terminate },
	{ "msvcrt.dll", "_CxxThrowException", (void*)crt__CxxThrowException },
	{ "msvcrt.dll", "_XcptFilter", (void*)crt__XcptFilter },
	{ "msvcrt.dll", "__C_specific_handler", (void*)crt___C_specific_handler },
	{ "msvcrt.dll", "__CxxFrameHandler3", (void*)crt___CxxFrameHandler3 },
	{ "msvcrt.dll", "__dllonexit", (void*)crt___dllonexit },
	{ "msvcrt.dll", "__isascii", (void*)crt___isascii },
	{ "msvcrt.dll", "__unDName", (void*)crt___unDName },
	{ "msvcrt.dll", "_amsg_exit", (void*)crt__amsg_exit },
	{ "msvcrt.dll", "_atoi64", (void*)crt__atoi64 },
	{ "msvcrt.dll", "_callnewh", (void*)crt__callnewh },
	{ "msvcrt.dll", "_chsize_s", (void*)crt__chsize_s },
	{ "msvcrt.dll", "_clearfp", (void*)crt__clearfp },
	{ "msvcrt.dll", "_close", (void*)crt__close },
	{ "msvcrt.dll", "_controlfp", (void*)crt__controlfp },
	{ "msvcrt.dll", "_errno", (void*)crt__errno },
	{ "msvcrt.dll", "_filelengthi64", (void*)crt__filelengthi64 },
	{ "msvcrt.dll", "_fileno", (void*)crt__fileno },
	{ "msvcrt.dll", "_finite", (void*)crt__finite },
	{ "msvcrt.dll", "_fpclass", (void*)crt__fpclass },
	{ "msvcrt.dll", "_get_osfhandle", (void*)crt__get_osfhandle },
	{ "msvcrt.dll", "_initterm", (void*)crt__initterm },
	{ "msvcrt.dll", "_isnan", (void*)crt__isnan },
	{ "msvcrt.dll", "_lock", (void*)crt__lock },
	{ "msvcrt.dll", "_lseeki64", (void*)crt__lseeki64 },
	{ "msvcrt.dll", "_mbscmp", (void*)crt__mbscmp },
	{ "msvcrt.dll", "_mbstrlen", (void*)crt__mbstrlen },
	{ "msvcrt.dll", "_memicmp", (void*)crt__memicmp },
	{ "msvcrt.dll", "_onexit", (void*)crt__onexit },
	{ "msvcrt.dll", "_open_osfhandle", (void*)crt__open_osfhandle },
	{ "msvcrt.dll", "_purecall", (void*)crt__purecall },
	{ "msvcrt.dll", "_read", (void*)crt__read },
	{ "msvcrt.dll", "_snwprintf_s", (void*)crt__snwprintf_s },
	{ "msvcrt.dll", "_strdup", (void*)crt__strdup },
	{ "msvcrt.dll", "_stricmp", (void*)crt__stricmp },
	{ "msvcrt.dll", "_strnicmp", (void*)crt__strnicmp },
	{ "msvcrt.dll", "_strtoui64", (void*)crt__strtoui64 },
	{ "msvcrt.dll", "_time64", (void*)crt__time64 },
	{ "msvcrt.dll", "_unlock", (void*)crt__unlock },
	{ "msvcrt.dll", "_vsnprintf", (void*)crt__vsnprintf },
	{ "msvcrt.dll", "_vsnwprintf", (void*)crt__vsnwprintf },
	{ "msvcrt.dll", "_wcsdup", (void*)crt__wcsdup },
	{ "msvcrt.dll", "_wcsicmp", (void*)crt__wcsicmp },
	{ "msvcrt.dll", "_wcsnicmp", (void*)crt__wcsnicmp },
	{ "msvcrt.dll", "_wfsopen", (void*)crt__wfsopen },
	{ "msvcrt.dll", "_wfullpath", (void*)crt__wfullpath },
	{ "msvcrt.dll", "_wgetenv", (void*)crt__wgetenv },
	{ "msvcrt.dll", "_wmakepath_s", (void*)crt__wmakepath_s },
	{ "msvcrt.dll", "_write", (void*)crt__write },
	{ "msvcrt.dll", "_wsopen", (void*)crt__wsopen },
	{ "msvcrt.dll", "_wsplitpath_s", (void*)crt__wsplitpath_s },
	{ "msvcrt.dll", "acos", (void*)crt_acos },
	{ "msvcrt.dll", "asin", (void*)crt_asin },
	{ "msvcrt.dll", "atan", (void*)crt_atan },
	{ "msvcrt.dll", "atan2", (void*)crt_atan2 },
	{ "msvcrt.dll", "atof", (void*)crt_atof },
	{ "msvcrt.dll", "atoi", (void*)crt_atoi },
	{ "msvcrt.dll", "bsearch", (void*)crt_bsearch },
	{ "msvcrt.dll", "ceil", (void*)crt_ceil },
	{ "msvcrt.dll", "cos", (void*)crt_cos },
	{ "msvcrt.dll", "cosh", (void*)crt_cosh },
	{ "msvcrt.dll", "exp", (void*)crt_exp },
	{ "msvcrt.dll", "fclose", (void*)crt_fclose },
	{ "msvcrt.dll", "floor", (void*)crt_floor },
	{ "msvcrt.dll", "floorf", (void*)crt_floorf },
	{ "msvcrt.dll", "fmod", (void*)crt_fmod },
	{ "msvcrt.dll", "fread", (void*)crt_fread },
	{ "msvcrt.dll", "free", (void*)crt_free },
	{ "msvcrt.dll", "fseek", (void*)crt_fseek },
	{ "msvcrt.dll", "ftell", (void*)crt_ftell },
	{ "msvcrt.dll", "getenv", (void*)crt_getenv },
	{ "msvcrt.dll", "isalnum", (void*)crt_isalnum },
	{ "msvcrt.dll", "isalpha", (void*)crt_isalpha },
	{ "msvcrt.dll", "isdigit", (void*)crt_isdigit },
	{ "msvcrt.dll", "isspace", (void*)crt_isspace },
	{ "msvcrt.dll", "isxdigit", (void*)crt_isxdigit },
	{ "msvcrt.dll", "log", (void*)crt_log },
	{ "msvcrt.dll", "malloc", (void*)crt_malloc },
	{ "msvcrt.dll", "memcmp", (void*)crt_memcmp },
	{ "msvcrt.dll", "memcpy", (void*)crt_memcpy },
	{ "msvcrt.dll", "memcpy_s", (void*)crt_memcpy_s },
	{ "msvcrt.dll", "memmove", (void*)crt_memmove },
	{ "msvcrt.dll", "memset", (void*)crt_memset },
	{ "msvcrt.dll", "modf", (void*)crt_modf },
	{ "msvcrt.dll", "pow", (void*)crt_pow },
	{ "msvcrt.dll", "qsort", (void*)crt_qsort },
	{ "msvcrt.dll", "setlocale", (void*)crt_setlocale },
	{ "msvcrt.dll", "sin", (void*)crt_sin },
	{ "msvcrt.dll", "sinh", (void*)crt_sinh },
	{ "msvcrt.dll", "sprintf_s", (void*)crt_sprintf_s },
	{ "msvcrt.dll", "sqrt", (void*)crt_sqrt },
	{ "msvcrt.dll", "sscanf_s", (void*)crt_sscanf_s },
	{ "msvcrt.dll", "strcat_s", (void*)crt_strcat_s },
	{ "msvcrt.dll", "strchr", (void*)crt_strchr },
	{ "msvcrt.dll", "strcmp", (void*)crt_strcmp },
	{ "msvcrt.dll", "strcpy_s", (void*)crt_strcpy_s },
	{ "msvcrt.dll", "strncmp", (void*)crt_strncmp },
	{ "msvcrt.dll", "strncpy_s", (void*)crt_strncpy_s },
	{ "msvcrt.dll", "strnlen", (void*)crt_strnlen },
	{ "msvcrt.dll", "strrchr", (void*)crt_strrchr },
	{ "msvcrt.dll", "strstr", (void*)crt_strstr },
	{ "msvcrt.dll", "strtod", (void*)crt_strtod },
	{ "msvcrt.dll", "strtoul", (void*)crt_strtoul },
	{ "msvcrt.dll", "swprintf_s", (void*)crt_swprintf_s },
	{ "msvcrt.dll", "tan", (void*)crt_tan },
	{ "msvcrt.dll", "tanh", (void*)crt_tanh },
	{ "msvcrt.dll", "tolower", (void*)crt_tolower },
	{ "msvcrt.dll", "toupper", (void*)crt_toupper },
	{ "msvcrt.dll", "towlower", (void*)crt_towlower },
	{ "msvcrt.dll", "wcscat_s", (void*)crt_wcscat_s },
	{ "msvcrt.dll", "wcscpy_s", (void*)crt_wcscpy_s },
	{ "msvcrt.dll", "wcslen", (void*)crt_wcslen },
	{ "msvcrt.dll", "wcsncat_s", (void*)crt_wcsncat_s },
	{ "msvcrt.dll", "wcsncmp", (void*)crt_wcsncmp },
	{ "msvcrt.dll", "wcsncpy_s", (void*)crt_wcsncpy_s },
	{ "msvcrt.dll", "wcsrchr", (void*)crt_wcsrchr },
	{ "msvcrt.dll", "wcstoul", (void*)crt_wcstoul },
	{ NULL, NULL, NULL },
};
