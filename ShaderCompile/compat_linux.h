// The MSVC CRT functions ShaderCompile uses, for the Linux build, with the MSVC semantics the callers rely on
#pragma once

#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <strings.h>

inline int _stricmp( const char* a, const char* b )
{
	return strcasecmp( a, b );
}

inline char* _strdup( const char* s )
{
	return strdup( s );
}

inline int strcpy_s( char* dst, size_t dstSize, const char* src )
{
	if ( !dst || !dstSize )
		return 22;
	const size_t n = strlen( src );
	if ( n >= dstSize )
	{
		dst[0] = 0;
		return 34;
	}
	memcpy( dst, src, n + 1 );
	return 0;
}

template <size_t N>
inline int strcpy_s( char ( &dst )[N], const char* src )
{
	return strcpy_s( dst, N, src );
}

inline int _strupr_s( char* s, size_t size )
{
	for ( size_t i = 0; i < size && s[i]; ++i )
		s[i] = static_cast<char>( toupper( static_cast<unsigned char>( s[i] ) ) );
	return 0;
}

template <size_t N>
inline int _strupr_s( char ( &s )[N] )
{
	return _strupr_s( s, N );
}

// MSVC's sprintf_s fails the whole call on overflow where snprintf truncates, the callers size their buffers
inline int sprintf_s( char* buf, size_t size, const char* fmt, ... ) __attribute__( ( format( printf, 3, 4 ) ) );
inline int sprintf_s( char* buf, size_t size, const char* fmt, ... )
{
	va_list ap;
	va_start( ap, fmt );
	const int n = vsnprintf( buf, size, fmt, ap );
	va_end( ap );
	if ( n < 0 || static_cast<size_t>( n ) >= size )
	{
		if ( size )
			buf[0] = 0;
		return -1;
	}
	return n;
}

template <size_t N>
inline int sprintf_s( char ( &buf )[N], const char* fmt, ... ) __attribute__( ( format( printf, 2, 3 ) ) );
template <size_t N>
inline int sprintf_s( char ( &buf )[N], const char* fmt, ... )
{
	va_list ap;
	va_start( ap, fmt );
	const int n = vsnprintf( buf, N, fmt, ap );
	va_end( ap );
	if ( n < 0 || static_cast<size_t>( n ) >= N )
	{
		buf[0] = 0;
		return -1;
	}
	return n;
}
