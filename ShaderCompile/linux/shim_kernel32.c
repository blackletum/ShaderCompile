// kernel32 as d3dcompiler_47.dll uses it, see shim.h for the ground rules.
//
// Traced compiles exercise the heap, the VirtualAlloc arena, critical sections, TLS, a handful of file and
// path calls and the string conversions, see the table at the bottom for what is real.
// The file-mapping and the Rtl unwinding imports are left to the loader's traps on purpose,
// the first is a replay-capture feature and the second is exception dispatch, neither runs in a compile
#define _GNU_SOURCE
#include "shim.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define K32( ret, name ) static MSABI ret k32_##name

static void set_last_error( DWORD e )
{
	shim_current_teb()->LastErrorValue = e;
}

K32( DWORD, GetLastError )( void )
{
	return shim_current_teb()->LastErrorValue;
}

K32( void, SetLastError )( DWORD e )
{
	set_last_error( e );
}

K32( HANDLE, GetCurrentProcess )( void )
{
	return (HANDLE)(intptr_t)-1;
}

K32( DWORD, GetCurrentProcessId )( void )
{
	return (DWORD)getpid();
}

K32( DWORD, GetCurrentThreadId )( void )
{
	return (DWORD)syscall( SYS_gettid );
}

K32( void, Sleep )( DWORD ms )
{
	usleep( (useconds_t)ms * 1000 );
}

K32( BOOL, TerminateProcess )( HANDLE process, UINT code )
{
	SHIM_UNUSED( process );
	fprintf( stderr, "\nShaderCompile: d3dcompiler_47.dll asked to terminate the process with code %u\n", code );
	_exit( (int)code );
}

K32( void*, SetUnhandledExceptionFilter )( void* filter )
{
	SHIM_UNUSED( filter );
	return NULL;
}

K32( LONG, UnhandledExceptionFilter )( void* info )
{
	SHIM_UNUSED( info );
	return 0; // EXCEPTION_CONTINUE_SEARCH
}

K32( BOOL, DisableThreadLibraryCalls )( HMODULE m )
{
	SHIM_UNUSED( m );
	return TRUE;
}

K32( void, OutputDebugStringA )( LPCSTR s )
{
	shim_trace( "OutputDebugStringA: %s", s ? s : "(null)" );
}

K32( BOOL, QueryPerformanceCounter )( LARGE_INTEGER* out )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	out->QuadPart = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
	return TRUE;
}

K32( DWORD, GetTickCount )( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (DWORD)( (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000 );
}

// 100 ns ticks since 1601-01-01, the Windows epoch is 11644473600 seconds before Unix's
K32( void, GetSystemTimeAsFileTime )( FILETIME* ft )
{
	struct timespec ts;
	clock_gettime( CLOCK_REALTIME, &ts );
	uint64_t t = ( (uint64_t)ts.tv_sec + 11644473600ull ) * 10000000ull + (uint64_t)ts.tv_nsec / 100;
	ft->dwLowDateTime = (DWORD)t;
	ft->dwHighDateTime = (DWORD)( t >> 32 );
}

typedef struct SYSTEM_INFO
{
	WORD wProcessorArchitecture;
	WORD wReserved;
	DWORD dwPageSize;
	void* lpMinimumApplicationAddress;
	void* lpMaximumApplicationAddress;
	uintptr_t dwActiveProcessorMask;
	DWORD dwNumberOfProcessors;
	DWORD dwProcessorType;
	DWORD dwAllocationGranularity;
	WORD wProcessorLevel;
	WORD wProcessorRevision;
} SYSTEM_INFO;
_Static_assert( sizeof( SYSTEM_INFO ) == 48, "SYSTEM_INFO size" );

K32( void, GetSystemInfo )( SYSTEM_INFO* si )
{
	memset( si, 0, sizeof( *si ) );
	long cpus = sysconf( _SC_NPROCESSORS_ONLN );
	if ( cpus < 1 )
		cpus = 1;
	if ( cpus > 64 )
		cpus = 64;
	si->wProcessorArchitecture = 9; // PROCESSOR_ARCHITECTURE_AMD64
	si->dwPageSize = 4096;
	si->lpMinimumApplicationAddress = (void*)0x10000;
	si->lpMaximumApplicationAddress = (void*)0x00007FFFFFFEFFFFull;
	si->dwActiveProcessorMask = cpus == 64 ? ~(uintptr_t)0 : ( ( (uintptr_t)1 << cpus ) - 1 );
	si->dwNumberOfProcessors = (DWORD)cpus;
	si->dwProcessorType = 8664; // PROCESSOR_AMD_X8664
	si->dwAllocationGranularity = 0x10000;
	si->wProcessorLevel = 6;
	si->wProcessorRevision = 0;
}

#define HEAP_ZERO_MEMORY 0x8

// Every heap is malloc, a heap handle only has to be distinct and non-null
static uintptr_t next_heap = 0x20;

K32( HANDLE, GetProcessHeap )( void )
{
	return (HANDLE)0x10;
}

K32( HANDLE, HeapCreate )( DWORD options, SIZE_T initial, SIZE_T maximum )
{
	SHIM_UNUSED( options );
	SHIM_UNUSED( initial );
	SHIM_UNUSED( maximum );
	return (HANDLE)__atomic_fetch_add( &next_heap, 0x10, __ATOMIC_RELAXED );
}

K32( BOOL, HeapDestroy )( HANDLE heap )
{
	SHIM_UNUSED( heap );
	return TRUE;
}

K32( LPVOID, HeapAlloc )( HANDLE heap, DWORD flags, SIZE_T size )
{
	SHIM_UNUSED( heap );
	// A zero-byte allocation still returns a block on Windows
	void* p = ( flags & HEAP_ZERO_MEMORY ) ? calloc( 1, size ? size : 1 ) : malloc( size ? size : 1 );
	if ( !p )
		set_last_error( ERROR_NOT_ENOUGH_MEMORY );
	return p;
}

K32( BOOL, HeapFree )( HANDLE heap, DWORD flags, LPVOID p )
{
	SHIM_UNUSED( heap );
	SHIM_UNUSED( flags );
	free( p );
	return TRUE;
}

#define LMEM_ZEROINIT 0x40

K32( void*, LocalAlloc )( UINT flags, SIZE_T bytes )
{
	return ( flags & LMEM_ZEROINIT ) ? calloc( 1, bytes ? bytes : 1 ) : malloc( bytes ? bytes : 1 );
}

K32( void*, LocalFree )( void* p )
{
	free( p );
	return NULL;
}

#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_WRITECOPY 0x08
#define PAGE_EXECUTE 0x10
#define PAGE_EXECUTE_READ 0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80

// The DLL runs an arena on top of VirtualAlloc, one reservation of a megabyte and 64 KB commits inside it.
// A reservation is mmap(PROT_NONE) at 64 KB granularity, a commit is mprotect,
// and a release is munmap of the whole reservation, which is why its size has to be remembered here
typedef struct reservation
{
	uint8_t* base;
	size_t size;
} reservation;

static reservation* reservations;
static size_t reservation_count, reservation_cap;
static pthread_mutex_t vm_lock = PTHREAD_MUTEX_INITIALIZER;

static int page_prot( DWORD p )
{
	switch ( p & 0xFF )
	{
	case PAGE_NOACCESS: return PROT_NONE;
	case PAGE_READONLY: return PROT_READ;
	case PAGE_READWRITE:
	case PAGE_WRITECOPY: return PROT_READ | PROT_WRITE;
	case PAGE_EXECUTE: return PROT_EXEC;
	case PAGE_EXECUTE_READ: return PROT_READ | PROT_EXEC;
	case PAGE_EXECUTE_READWRITE:
	case PAGE_EXECUTE_WRITECOPY: return PROT_READ | PROT_WRITE | PROT_EXEC;
	default: return PROT_READ | PROT_WRITE;
	}
}

static reservation* find_reservation( const void* addr )
{
	for ( size_t i = 0; i < reservation_count; ++i )
		if ( (const uint8_t*)addr >= reservations[i].base && (const uint8_t*)addr < reservations[i].base + reservations[i].size )
			return &reservations[i];
	return NULL;
}

K32( LPVOID, VirtualAlloc )( LPVOID addr, SIZE_T size, DWORD type, DWORD protect )
{
	const size_t granularity = 0x10000, page = 4096;
	pthread_mutex_lock( &vm_lock );
	void* result = NULL;
	if ( type & MEM_RESERVE )
	{
		size_t len = ( size + granularity - 1 ) & ~( granularity - 1 );
		int prot = ( type & MEM_COMMIT ) ? page_prot( protect ) : PROT_NONE;
		uint8_t* base;
		if ( addr )
		{
			base = mmap( (void*)( (uintptr_t)addr & ~( granularity - 1 ) ), len, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0 );
			if ( base == MAP_FAILED )
				base = NULL;
		}
		else
		{
			// Over-map by one granule and trim so the base is 64 KB aligned, as the arena assumes
			uint8_t* raw = mmap( NULL, len + granularity, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0 );
			if ( raw == MAP_FAILED )
				base = NULL;
			else
			{
				base = (uint8_t*)( ( (uintptr_t)raw + granularity - 1 ) & ~( granularity - 1 ) );
				if ( base > raw )
					munmap( raw, (size_t)( base - raw ) );
				size_t tail = granularity - (size_t)( base - raw );
				if ( tail )
					munmap( base + len, tail );
			}
		}
		if ( base )
		{
			if ( reservation_count == reservation_cap )
			{
				reservation_cap = reservation_cap ? reservation_cap * 2 : 64;
				reservations = realloc( reservations, reservation_cap * sizeof( reservation ) );
			}
			reservations[reservation_count++] = (reservation){ base, len };
			result = base;
		}
	}
	else if ( type & MEM_COMMIT )
	{
		if ( !addr )
		{
			// Commit without a reservation, Windows reserves and commits together
			pthread_mutex_unlock( &vm_lock );
			return k32_VirtualAlloc( NULL, size, MEM_RESERVE | MEM_COMMIT, protect );
		}
		uint8_t* start = (uint8_t*)( (uintptr_t)addr & ~( page - 1 ) );
		size_t len = ( ( (uintptr_t)addr + size + page - 1 ) & ~( page - 1 ) ) - (uintptr_t)start;
		if ( find_reservation( start ) && mprotect( start, len, page_prot( protect ) ) == 0 )
			result = start;
	}
	pthread_mutex_unlock( &vm_lock );
	if ( !result )
		set_last_error( ERROR_NOT_ENOUGH_MEMORY );
	shim_trace( "VirtualAlloc(%p, %zu, %#x, %#x) -> %p", addr, (size_t)size, type, protect, result );
	return result;
}

K32( BOOL, VirtualFree )( LPVOID addr, SIZE_T size, DWORD type )
{
	BOOL ok = FALSE;
	pthread_mutex_lock( &vm_lock );
	reservation* r = find_reservation( addr );
	if ( r && ( type & MEM_RELEASE ) )
	{
		munmap( r->base, r->size );
		*r = reservations[--reservation_count];
		ok = TRUE;
	}
	else if ( r && ( type & MEM_DECOMMIT ) )
	{
		uint8_t* start = (uint8_t*)( (uintptr_t)addr & ~(uintptr_t)4095 );
		size_t len = size ? ( ( (uintptr_t)addr + size + 4095 ) & ~(uintptr_t)4095 ) - (uintptr_t)start : (size_t)( r->base + r->size - start );
		ok = mprotect( start, len, PROT_NONE ) == 0;
		madvise( start, len, MADV_DONTNEED );
	}
	pthread_mutex_unlock( &vm_lock );
	if ( !ok )
		set_last_error( ERROR_INVALID_PARAMETER );
	shim_trace( "VirtualFree(%p, %zu, %#x) -> %d", addr, (size_t)size, type, ok );
	return ok;
}

static pthread_mutex_t* cs_mutex( CRITICAL_SECTION* cs )
{
	return (pthread_mutex_t*)cs->LockSemaphore;
}

K32( void, InitializeCriticalSection )( CRITICAL_SECTION* cs )
{
	memset( cs, 0, sizeof( *cs ) );
	pthread_mutex_t* m = malloc( sizeof( *m ) );
	pthread_mutexattr_t attr;
	pthread_mutexattr_init( &attr );
	pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_RECURSIVE );
	pthread_mutex_init( m, &attr );
	pthread_mutexattr_destroy( &attr );
	cs->LockSemaphore = m;
	cs->LockCount = -1;
}

K32( BOOL, InitializeCriticalSectionAndSpinCount )( CRITICAL_SECTION* cs, DWORD spin )
{
	k32_InitializeCriticalSection( cs );
	cs->SpinCount = spin;
	return TRUE;
}

K32( void, EnterCriticalSection )( CRITICAL_SECTION* cs )
{
	pthread_mutex_lock( cs_mutex( cs ) );
	cs->RecursionCount++;
	cs->OwningThread = (HANDLE)(uintptr_t)syscall( SYS_gettid );
}

K32( void, LeaveCriticalSection )( CRITICAL_SECTION* cs )
{
	if ( --cs->RecursionCount == 0 )
		cs->OwningThread = NULL;
	pthread_mutex_unlock( cs_mutex( cs ) );
}

K32( void, DeleteCriticalSection )( CRITICAL_SECTION* cs )
{
	if ( cs->LockSemaphore )
	{
		pthread_mutex_destroy( cs_mutex( cs ) );
		free( cs->LockSemaphore );
	}
	memset( cs, 0, sizeof( *cs ) );
}

static _Atomic uint64_t tls_in_use;

K32( DWORD, TlsAlloc )( void )
{
	for ( ;; )
	{
		uint64_t cur = atomic_load( &tls_in_use );
		if ( cur == ~0ull )
		{
			set_last_error( ERROR_NOT_ENOUGH_MEMORY );
			return TLS_OUT_OF_INDEXES;
		}
		unsigned idx = (unsigned)__builtin_ctzll( ~cur );
		if ( atomic_compare_exchange_weak( &tls_in_use, &cur, cur | ( 1ull << idx ) ) )
			return idx;
	}
}

K32( BOOL, TlsFree )( DWORD idx )
{
	if ( idx >= 64 )
	{
		set_last_error( ERROR_INVALID_PARAMETER );
		return FALSE;
	}
	atomic_fetch_and( &tls_in_use, ~( 1ull << idx ) );
	return TRUE;
}

K32( LPVOID, TlsGetValue )( DWORD idx )
{
	if ( idx >= 64 )
	{
		set_last_error( ERROR_INVALID_PARAMETER );
		return NULL;
	}
	shim_teb* teb = shim_current_teb();
	teb->LastErrorValue = ERROR_SUCCESS; // Windows clears it so a null slot can be told from a failure
	return teb->TlsSlots[idx];
}

K32( BOOL, TlsSetValue )( DWORD idx, LPVOID value )
{
	if ( idx >= 64 )
	{
		set_last_error( ERROR_INVALID_PARAMETER );
		return FALSE;
	}
	shim_current_teb()->TlsSlots[idx] = value;
	return TRUE;
}

// The DLL itself is the only real module.
// The system DLLs it might ask for by name get a pseudo handle each so GetProcAddress can answer from the shim
// tables, and anything else is not found, which is how an optional feature is probed for on Windows too
static const char* const pseudo_modules[] = { "kernel32", "kernelbase", "ntdll", "msvcrt", "advapi32", "rpcrt4" };

static HMODULE pseudo_handle( const char* name )
{
	// Strip any path and extension
	const char* p = strrchr( name, '\\' );
	if ( p )
		name = p + 1;
	p = strrchr( name, '/' );
	if ( p )
		name = p + 1;
	size_t len = strlen( name );
	if ( len > 4 && !strcasecmp( name + len - 4, ".dll" ) )
		len -= 4;
	for ( size_t i = 0; i < sizeof( pseudo_modules ) / sizeof( pseudo_modules[0] ); ++i )
		if ( strlen( pseudo_modules[i] ) == len && !strncasecmp( pseudo_modules[i], name, len ) )
			return (HMODULE)( 0x1000 + 0x10 * i );
	return NULL;
}

K32( HMODULE, LoadLibraryExW )( LPCWSTR name, HANDLE file, DWORD flags )
{
	SHIM_UNUSED( file );
	SHIM_UNUSED( flags );
	char buf[260];
	size_t n = shim_utf16_to_utf8( name, shim_wcslen( name ), buf, sizeof( buf ) - 1 );
	buf[n] = 0;
	HMODULE h = pseudo_handle( buf );
	if ( !h && !strcasecmp( buf, "d3dcompiler_47.dll" ) )
		h = shim_dll_module();
	shim_trace( "LoadLibraryExW(%s) -> %p", buf, (void*)h );
	if ( !h )
		set_last_error( ERROR_MOD_NOT_FOUND );
	return h;
}

K32( BOOL, FreeLibrary )( HMODULE m )
{
	SHIM_UNUSED( m );
	return TRUE;
}

K32( FARPROC, GetProcAddress )( HMODULE m, LPCSTR name )
{
	FARPROC fn = NULL;
	if ( (uintptr_t)name < 0x10000 )
	{
		set_last_error( ERROR_PROC_NOT_FOUND ); // by ordinal, nothing here has ordinals
		return NULL;
	}
	if ( m == shim_dll_module() )
		fn = shim_dll_export( name );
	else
	{
		const shim_entry* tables[] = { shim_kernel32_table, shim_msvcrt_table, shim_advapi32_table };
		for ( size_t t = 0; t < 3 && !fn; ++t )
			for ( const shim_entry* e = tables[t]; e->name; ++e )
				if ( !strcmp( e->name, name ) )
				{
					fn = e->fn;
					break;
				}
	}
	shim_trace( "GetProcAddress(%p, %s) -> %p", (void*)m, name, fn );
	if ( !fn )
		set_last_error( ERROR_PROC_NOT_FOUND );
	return fn;
}

K32( DWORD, GetModuleFileNameA )( HMODULE m, LPSTR buf, DWORD size )
{
	SHIM_UNUSED( m );
	// The only module there is, whichever handle is asked about
	const char* path = shim_dll_path();
	size_t len = strlen( path );
	if ( !size )
		return 0;
	if ( len >= size )
	{
		memcpy( buf, path, size - 1 );
		buf[size - 1] = 0;
		set_last_error( ERROR_INSUFFICIENT_BUFFER );
		return size;
	}
	memcpy( buf, path, len + 1 );
	return (DWORD)len;
}

K32( DWORD, GetEnvironmentVariableA )( LPCSTR name, LPSTR buf, DWORD size )
{
	const char* v = getenv( name );
	if ( !v )
	{
		set_last_error( ERROR_ENVVAR_NOT_FOUND );
		return 0;
	}
	size_t len = strlen( v );
	if ( !buf || size <= len )
		return (DWORD)len + 1;
	memcpy( buf, v, len + 1 );
	return (DWORD)len;
}

// A Windows path from the DLL to one open() takes, backslashes to slashes and a Z: drive, Wine's root, dropped.
// ShaderCompile hands the DLL plain names and the DLL only opens files for its replay feature,
// so this sees little traffic
static void to_posix_path( const char* in, char* out, size_t outLen )
{
	size_t j = 0;
	if ( ( ( in[0] >= 'A' && in[0] <= 'Z' ) || ( in[0] >= 'a' && in[0] <= 'z' ) ) && in[1] == ':' )
		in += 2;
	for ( ; *in && j + 1 < outLen; ++in )
		out[j++] = *in == '\\' ? '/' : *in;
	out[j] = 0;
}

static void wide_to_posix_path( LPCWSTR in, char* out, size_t outLen )
{
	char tmp[PATH_MAX];
	size_t n = shim_utf16_to_utf8( in, shim_wcslen( in ), tmp, sizeof( tmp ) - 1 );
	tmp[n] = 0;
	to_posix_path( tmp, out, outLen );
}

K32( DWORD, GetFullPathNameA )( LPCSTR name, DWORD bufLen, LPSTR buf, LPSTR* filePart )
{
	char full[PATH_MAX * 2];
	if ( name[0] == '/' || name[0] == '\\' || ( name[0] && name[1] == ':' ) )
		snprintf( full, sizeof( full ), "%s", name );
	else
	{
		char cwd[PATH_MAX];
		if ( !getcwd( cwd, sizeof( cwd ) ) )
			cwd[0] = 0;
		snprintf( full, sizeof( full ), "%s/%s", cwd, name );
	}
	size_t len = strlen( full );
	if ( len + 1 > bufLen )
		return (DWORD)len + 1;
	memcpy( buf, full, len + 1 );
	if ( filePart )
	{
		char* slash = strrchr( buf, '/' );
		char* bslash = strrchr( buf, '\\' );
		char* last = slash > bslash ? slash : bslash;
		*filePart = last ? last + 1 : buf;
	}
	return (DWORD)len;
}

K32( DWORD, GetFullPathNameW )( LPCWSTR name, DWORD bufLen, LPWSTR buf, LPWSTR* filePart )
{
	char narrow[PATH_MAX], full[PATH_MAX * 2];
	size_t n = shim_utf16_to_utf8( name, shim_wcslen( name ), narrow, sizeof( narrow ) - 1 );
	narrow[n] = 0;
	char* part = NULL;
	DWORD r = k32_GetFullPathNameA( narrow, sizeof( full ), full, &part );
	if ( !r || r > sizeof( full ) )
		return 0;
	size_t wlen = shim_utf8_to_utf16( full, strlen( full ), NULL, 0 );
	if ( wlen + 1 > bufLen )
		return (DWORD)wlen + 1;
	shim_utf8_to_utf16( full, strlen( full ), buf, bufLen );
	buf[wlen] = 0;
	if ( filePart )
		*filePart = buf + shim_utf8_to_utf16( full, (size_t)( part - full ), NULL, 0 );
	return (DWORD)wlen;
}

#define FILE_ATTRIBUTE_READONLY 0x01
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL 0x80

K32( DWORD, GetFileAttributesW )( LPCWSTR name )
{
	char path[PATH_MAX];
	wide_to_posix_path( name, path, sizeof( path ) );
	struct stat st;
	if ( stat( path, &st ) )
	{
		set_last_error( errno == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED );
		return INVALID_FILE_ATTRIBUTES;
	}
	DWORD a = S_ISDIR( st.st_mode ) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
	if ( !( st.st_mode & S_IWUSR ) )
		a |= FILE_ATTRIBUTE_READONLY;
	return a;
}

K32( BOOL, SetFileAttributesW )( LPCWSTR name, DWORD attrs )
{
	char path[PATH_MAX];
	wide_to_posix_path( name, path, sizeof( path ) );
	struct stat st;
	if ( stat( path, &st ) )
	{
		set_last_error( ERROR_FILE_NOT_FOUND );
		return FALSE;
	}
	mode_t mode = ( attrs & FILE_ATTRIBUTE_READONLY ) ? ( st.st_mode & ~(mode_t)( S_IWUSR | S_IWGRP | S_IWOTH ) ) : ( st.st_mode | S_IWUSR );
	return chmod( path, mode ) == 0;
}

K32( BOOL, DeleteFileW )( LPCWSTR name )
{
	char path[PATH_MAX];
	wide_to_posix_path( name, path, sizeof( path ) );
	if ( unlink( path ) )
	{
		set_last_error( errno == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_ACCESS_DENIED );
		return FALSE;
	}
	return TRUE;
}

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_TYPE_UNKNOWN 0
#define FILE_TYPE_DISK 1
#define FILE_TYPE_CHAR 2
#define FILE_TYPE_PIPE 3

// A file HANDLE is the descriptor plus SHIM_HANDLE_BIAS, anything outside that range is a pseudo handle
static int handle_fd( HANDLE h )
{
	intptr_t v = (intptr_t)h;
	return v >= SHIM_HANDLE_BIAS && v < SHIM_HANDLE_BIAS + 0x100000 ? (int)( v - SHIM_HANDLE_BIAS ) : -1;
}

static HANDLE open_posix( const char* path, DWORD access, DWORD disposition )
{
	int flags = 0;
	if ( ( access & GENERIC_READ ) && ( access & GENERIC_WRITE ) )
		flags = O_RDWR;
	else if ( access & GENERIC_WRITE )
		flags = O_WRONLY;
	else
		flags = O_RDONLY;
	switch ( disposition )
	{
	case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
	case CREATE_ALWAYS: flags |= O_CREAT | O_TRUNC; break;
	case OPEN_ALWAYS: flags |= O_CREAT; break;
	case TRUNCATE_EXISTING: flags |= O_TRUNC; break;
	default: break;
	}
	int fd = open( path, flags | O_CLOEXEC, 0644 );
	if ( fd < 0 )
	{
		set_last_error( errno == ENOENT ? ERROR_FILE_NOT_FOUND : errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_ACCESS_DENIED );
		return INVALID_HANDLE_VALUE;
	}
	return (HANDLE)(intptr_t)( fd + SHIM_HANDLE_BIAS );
}

K32( HANDLE, CreateFileA )( LPCSTR name, DWORD access, DWORD share, void* security, DWORD disposition, DWORD attrs, HANDLE template_ )
{
	SHIM_UNUSED( share );
	SHIM_UNUSED( security );
	SHIM_UNUSED( attrs );
	SHIM_UNUSED( template_ );
	char path[PATH_MAX];
	to_posix_path( name, path, sizeof( path ) );
	HANDLE h = open_posix( path, access, disposition );
	shim_trace( "CreateFileA(%s, %#x, %u) -> %p", name, access, disposition, h );
	return h;
}

K32( HANDLE, CreateFileW )( LPCWSTR name, DWORD access, DWORD share, void* security, DWORD disposition, DWORD attrs, HANDLE template_ )
{
	SHIM_UNUSED( share );
	SHIM_UNUSED( security );
	SHIM_UNUSED( attrs );
	SHIM_UNUSED( template_ );
	char path[PATH_MAX];
	wide_to_posix_path( name, path, sizeof( path ) );
	HANDLE h = open_posix( path, access, disposition );
	shim_trace( "CreateFileW(%s, %#x, %u) -> %p", path, access, disposition, h );
	return h;
}

K32( BOOL, CloseHandle )( HANDLE h )
{
	int fd = handle_fd( h );
	if ( fd >= 0 )
		return close( fd ) == 0;
	// Pseudo handles for the process and the heaps close fine
	return TRUE;
}

K32( BOOL, ReadFile )( HANDLE h, LPVOID buf, DWORD n, LPDWORD read_, void* overlapped )
{
	SHIM_UNUSED( overlapped );
	int fd = handle_fd( h );
	if ( fd < 0 )
	{
		set_last_error( ERROR_INVALID_HANDLE );
		return FALSE;
	}
	ssize_t r = read( fd, buf, n );
	if ( r < 0 )
	{
		set_last_error( ERROR_ACCESS_DENIED );
		return FALSE;
	}
	if ( read_ )
		*read_ = (DWORD)r;
	return TRUE;
}

K32( BOOL, WriteFile )( HANDLE h, LPCVOID buf, DWORD n, LPDWORD written, void* overlapped )
{
	SHIM_UNUSED( overlapped );
	int fd = handle_fd( h );
	if ( fd < 0 )
	{
		set_last_error( ERROR_INVALID_HANDLE );
		return FALSE;
	}
	ssize_t w = write( fd, buf, n );
	if ( w < 0 )
	{
		set_last_error( ERROR_ACCESS_DENIED );
		return FALSE;
	}
	if ( written )
		*written = (DWORD)w;
	return TRUE;
}

K32( DWORD, GetFileType )( HANDLE h )
{
	int fd = handle_fd( h );
	struct stat st;
	if ( fd < 0 || fstat( fd, &st ) )
		return FILE_TYPE_UNKNOWN;
	if ( S_ISREG( st.st_mode ) || S_ISDIR( st.st_mode ) )
		return FILE_TYPE_DISK;
	if ( S_ISFIFO( st.st_mode ) || S_ISSOCK( st.st_mode ) )
		return FILE_TYPE_PIPE;
	return FILE_TYPE_CHAR;
}

K32( DWORD, GetFileSize )( HANDLE h, LPDWORD high )
{
	int fd = handle_fd( h );
	struct stat st;
	if ( fd < 0 || fstat( fd, &st ) )
	{
		set_last_error( ERROR_INVALID_HANDLE );
		return INVALID_FILE_SIZE;
	}
	if ( high )
		*high = (DWORD)( (uint64_t)st.st_size >> 32 );
	return (DWORD)st.st_size;
}

K32( BOOL, GetFileSizeEx )( HANDLE h, LARGE_INTEGER* size )
{
	int fd = handle_fd( h );
	struct stat st;
	if ( fd < 0 || fstat( fd, &st ) )
	{
		set_last_error( ERROR_INVALID_HANDLE );
		return FALSE;
	}
	size->QuadPart = st.st_size;
	return TRUE;
}

static int whence_of( DWORD method )
{
	return method == 1 ? SEEK_CUR : method == 2 ? SEEK_END : SEEK_SET;
}

K32( DWORD, SetFilePointer )( HANDLE h, LONG distance, LONG* distanceHigh, DWORD method )
{
	int fd = handle_fd( h );
	int64_t off = distanceHigh ? ( ( (int64_t)*distanceHigh << 32 ) | (uint32_t)distance ) : distance;
	off_t r = fd < 0 ? -1 : lseek( fd, off, whence_of( method ) );
	if ( r < 0 )
	{
		set_last_error( ERROR_INVALID_PARAMETER );
		return INVALID_SET_FILE_POINTER;
	}
	if ( distanceHigh )
		*distanceHigh = (LONG)( (uint64_t)r >> 32 );
	return (DWORD)r;
}

K32( BOOL, SetFilePointerEx )( HANDLE h, LARGE_INTEGER distance, LARGE_INTEGER* newPos, DWORD method )
{
	int fd = handle_fd( h );
	off_t r = fd < 0 ? -1 : lseek( fd, distance.QuadPart, whence_of( method ) );
	if ( r < 0 )
	{
		set_last_error( ERROR_INVALID_PARAMETER );
		return FALSE;
	}
	if ( newPos )
		newPos->QuadPart = r;
	return TRUE;
}

K32( BOOL, SetEndOfFile )( HANDLE h )
{
	int fd = handle_fd( h );
	off_t pos = fd < 0 ? -1 : lseek( fd, 0, SEEK_CUR );
	if ( pos < 0 || ftruncate( fd, pos ) )
	{
		set_last_error( ERROR_INVALID_HANDLE );
		return FALSE;
	}
	return TRUE;
}

#define CP_ACP 0
#define CP_OEMCP 1
#define CP_UTF8 65001

// Bytes above 0x7F in the ANSI code pages are taken as Latin-1, close enough to 1252 for file names,
// and the DLL's own text is ASCII
K32( int, MultiByteToWideChar )( UINT cp, DWORD flags, LPCSTR src, int srcLen, LPWSTR dst, int dstLen )
{
	SHIM_UNUSED( flags );
	if ( !src || srcLen == 0 )
	{
		set_last_error( ERROR_INVALID_PARAMETER );
		return 0;
	}
	size_t n = srcLen < 0 ? strlen( src ) + 1 : (size_t)srcLen; // -1 means include the terminator
	size_t need;
	if ( cp == CP_UTF8 )
		need = shim_utf8_to_utf16( src, n, NULL, 0 );
	else
		need = n;
	if ( dstLen == 0 )
		return (int)need;
	if ( (size_t)dstLen < need )
	{
		set_last_error( ERROR_INSUFFICIENT_BUFFER );
		return 0;
	}
	if ( cp == CP_UTF8 )
		shim_utf8_to_utf16( src, n, dst, (size_t)dstLen );
	else
		for ( size_t i = 0; i < n; ++i )
			dst[i] = (WCHAR)(uint8_t)src[i];
	return (int)need;
}

K32( int, WideCharToMultiByte )( UINT cp, DWORD flags, LPCWSTR src, int srcLen, LPSTR dst, int dstLen, LPCSTR defaultChar, LPBOOL usedDefault )
{
	SHIM_UNUSED( flags );
	if ( usedDefault )
		*usedDefault = FALSE;
	if ( !src || srcLen == 0 )
	{
		set_last_error( ERROR_INVALID_PARAMETER );
		return 0;
	}
	size_t n = srcLen < 0 ? shim_wcslen( src ) + 1 : (size_t)srcLen;
	size_t need;
	if ( cp == CP_UTF8 )
		need = shim_utf16_to_utf8( src, n, NULL, 0 );
	else
		need = n;
	if ( dstLen == 0 )
		return (int)need;
	if ( (size_t)dstLen < need )
	{
		set_last_error( ERROR_INSUFFICIENT_BUFFER );
		return 0;
	}
	if ( cp == CP_UTF8 )
		shim_utf16_to_utf8( src, n, dst, (size_t)dstLen );
	else
		for ( size_t i = 0; i < n; ++i )
		{
			if ( src[i] < 0x100 )
				dst[i] = (char)src[i];
			else
			{
				dst[i] = defaultChar ? *defaultChar : '?';
				if ( usedDefault )
					*usedDefault = TRUE;
			}
		}
	return (int)need;
}

K32( int, lstrcmpiA )( LPCSTR a, LPCSTR b )
{
	int r = strcasecmp( a, b );
	return r < 0 ? -1 : r > 0 ? 1 : 0;
}

#define LCMAP_LOWERCASE 0x100
#define LCMAP_UPPERCASE 0x200

// Only the case mappings, on the BMP's ASCII range, which is all a shader compiler maps.
// Anything else is a shim gap worth knowing about, so it is fatal rather than wrong
K32( int, LCMapStringW )( DWORD locale, DWORD flags, LPCWSTR src, int srcLen, LPWSTR dst, int dstLen )
{
	SHIM_UNUSED( locale );
	if ( !( flags & ( LCMAP_LOWERCASE | LCMAP_UPPERCASE ) ) || ( flags & ~( LCMAP_LOWERCASE | LCMAP_UPPERCASE ) ) )
		shim_fatal( "LCMapStringW flags %#x not implemented", flags );
	size_t n = srcLen < 0 ? shim_wcslen( src ) + 1 : (size_t)srcLen;
	if ( dstLen == 0 )
		return (int)n;
	if ( (size_t)dstLen < n )
	{
		set_last_error( ERROR_INSUFFICIENT_BUFFER );
		return 0;
	}
	for ( size_t i = 0; i < n; ++i )
	{
		WCHAR c = src[i];
		if ( ( flags & LCMAP_LOWERCASE ) && c >= 'A' && c <= 'Z' )
			c = (WCHAR)( c + 32 );
		else if ( ( flags & LCMAP_UPPERCASE ) && c >= 'a' && c <= 'z' )
			c = (WCHAR)( c - 32 );
		dst[i] = c;
	}
	return (int)n;
}

// %VAR% expansion, the Windows way, unknown variables are left as written
K32( DWORD, ExpandEnvironmentStringsW )( LPCWSTR src, LPWSTR dst, DWORD dstLen )
{
	char in[PATH_MAX], out[PATH_MAX * 2];
	size_t n = shim_utf16_to_utf8( src, shim_wcslen( src ), in, sizeof( in ) - 1 );
	in[n] = 0;
	size_t o = 0;
	for ( size_t i = 0; in[i] && o + 1 < sizeof( out ); )
	{
		if ( in[i] == '%' )
		{
			char* end = strchr( in + i + 1, '%' );
			if ( end )
			{
				*end = 0;
				const char* v = getenv( in + i + 1 );
				if ( v )
				{
					size_t vl = strlen( v );
					if ( o + vl + 1 < sizeof( out ) )
					{
						memcpy( out + o, v, vl );
						o += vl;
					}
					i = (size_t)( end - in ) + 1;
					continue;
				}
				*end = '%';
			}
		}
		out[o++] = in[i++];
	}
	out[o] = 0;
	size_t need = shim_utf8_to_utf16( out, o, NULL, 0 ) + 1;
	if ( dst && dstLen >= need )
	{
		shim_utf8_to_utf16( out, o, dst, dstLen );
		dst[need - 1] = 0;
	}
	return (DWORD)need;
}

const shim_entry shim_kernel32_table[] = {
	{ "KERNEL32.dll", "CloseHandle", (void*)k32_CloseHandle },
	{ "KERNEL32.dll", "CreateFileA", (void*)k32_CreateFileA },
	{ "KERNEL32.dll", "CreateFileW", (void*)k32_CreateFileW },
	{ "KERNEL32.dll", "DeleteCriticalSection", (void*)k32_DeleteCriticalSection },
	{ "KERNEL32.dll", "DeleteFileW", (void*)k32_DeleteFileW },
	{ "KERNEL32.dll", "DisableThreadLibraryCalls", (void*)k32_DisableThreadLibraryCalls },
	{ "KERNEL32.dll", "EnterCriticalSection", (void*)k32_EnterCriticalSection },
	{ "KERNEL32.dll", "ExpandEnvironmentStringsW", (void*)k32_ExpandEnvironmentStringsW },
	{ "KERNEL32.dll", "FreeLibrary", (void*)k32_FreeLibrary },
	{ "KERNEL32.dll", "GetCurrentProcess", (void*)k32_GetCurrentProcess },
	{ "KERNEL32.dll", "GetCurrentProcessId", (void*)k32_GetCurrentProcessId },
	{ "KERNEL32.dll", "GetCurrentThreadId", (void*)k32_GetCurrentThreadId },
	{ "KERNEL32.dll", "GetEnvironmentVariableA", (void*)k32_GetEnvironmentVariableA },
	{ "KERNEL32.dll", "GetFileAttributesW", (void*)k32_GetFileAttributesW },
	{ "KERNEL32.dll", "GetFileSize", (void*)k32_GetFileSize },
	{ "KERNEL32.dll", "GetFileSizeEx", (void*)k32_GetFileSizeEx },
	{ "KERNEL32.dll", "GetFileType", (void*)k32_GetFileType },
	{ "KERNEL32.dll", "GetFullPathNameA", (void*)k32_GetFullPathNameA },
	{ "KERNEL32.dll", "GetFullPathNameW", (void*)k32_GetFullPathNameW },
	{ "KERNEL32.dll", "GetLastError", (void*)k32_GetLastError },
	{ "KERNEL32.dll", "GetModuleFileNameA", (void*)k32_GetModuleFileNameA },
	{ "KERNEL32.dll", "GetProcAddress", (void*)k32_GetProcAddress },
	{ "KERNEL32.dll", "GetProcessHeap", (void*)k32_GetProcessHeap },
	{ "KERNEL32.dll", "GetSystemInfo", (void*)k32_GetSystemInfo },
	{ "KERNEL32.dll", "GetSystemTimeAsFileTime", (void*)k32_GetSystemTimeAsFileTime },
	{ "KERNEL32.dll", "GetTickCount", (void*)k32_GetTickCount },
	{ "KERNEL32.dll", "HeapAlloc", (void*)k32_HeapAlloc },
	{ "KERNEL32.dll", "HeapCreate", (void*)k32_HeapCreate },
	{ "KERNEL32.dll", "HeapDestroy", (void*)k32_HeapDestroy },
	{ "KERNEL32.dll", "HeapFree", (void*)k32_HeapFree },
	{ "KERNEL32.dll", "InitializeCriticalSection", (void*)k32_InitializeCriticalSection },
	{ "KERNEL32.dll", "InitializeCriticalSectionAndSpinCount", (void*)k32_InitializeCriticalSectionAndSpinCount },
	{ "KERNEL32.dll", "LCMapStringW", (void*)k32_LCMapStringW },
	{ "KERNEL32.dll", "LeaveCriticalSection", (void*)k32_LeaveCriticalSection },
	{ "KERNEL32.dll", "LoadLibraryExW", (void*)k32_LoadLibraryExW },
	{ "KERNEL32.dll", "LocalAlloc", (void*)k32_LocalAlloc },
	{ "KERNEL32.dll", "LocalFree", (void*)k32_LocalFree },
	{ "KERNEL32.dll", "MultiByteToWideChar", (void*)k32_MultiByteToWideChar },
	{ "KERNEL32.dll", "OutputDebugStringA", (void*)k32_OutputDebugStringA },
	{ "KERNEL32.dll", "QueryPerformanceCounter", (void*)k32_QueryPerformanceCounter },
	{ "KERNEL32.dll", "ReadFile", (void*)k32_ReadFile },
	{ "KERNEL32.dll", "SetEndOfFile", (void*)k32_SetEndOfFile },
	{ "KERNEL32.dll", "SetFileAttributesW", (void*)k32_SetFileAttributesW },
	{ "KERNEL32.dll", "SetFilePointer", (void*)k32_SetFilePointer },
	{ "KERNEL32.dll", "SetFilePointerEx", (void*)k32_SetFilePointerEx },
	{ "KERNEL32.dll", "SetLastError", (void*)k32_SetLastError },
	{ "KERNEL32.dll", "SetUnhandledExceptionFilter", (void*)k32_SetUnhandledExceptionFilter },
	{ "KERNEL32.dll", "Sleep", (void*)k32_Sleep },
	{ "KERNEL32.dll", "TerminateProcess", (void*)k32_TerminateProcess },
	{ "KERNEL32.dll", "TlsAlloc", (void*)k32_TlsAlloc },
	{ "KERNEL32.dll", "TlsFree", (void*)k32_TlsFree },
	{ "KERNEL32.dll", "TlsGetValue", (void*)k32_TlsGetValue },
	{ "KERNEL32.dll", "TlsSetValue", (void*)k32_TlsSetValue },
	{ "KERNEL32.dll", "UnhandledExceptionFilter", (void*)k32_UnhandledExceptionFilter },
	{ "KERNEL32.dll", "VirtualAlloc", (void*)k32_VirtualAlloc },
	{ "KERNEL32.dll", "VirtualFree", (void*)k32_VirtualFree },
	{ "KERNEL32.dll", "WideCharToMultiByte", (void*)k32_WideCharToMultiByte },
	{ "KERNEL32.dll", "WriteFile", (void*)k32_WriteFile },
	{ "KERNEL32.dll", "lstrcmpiA", (void*)k32_lstrcmpiA },
	{ NULL, NULL, NULL },
};
