// What the shims share, the Win64 types the DLL's imports are declared with, the calling-convention attribute,
// the per-thread TEB and the table the loader resolves imports against.
//
// Everything the DLL calls into is MSABI, Microsoft's x64 convention, RCX RDX R8 R9 and a 32-byte shadow space,
// which GCC applies per function with __attribute__((ms_abi)).
// THE TRAP: a libc function can never be handed to the DLL directly, the register assignment differs,
// memset(void*, int, size_t) arrives in RCX RDX R8 and glibc reads RDI RSI RDX, so every import is a wrapper here.
// THE TRAP: long is 32 bits on Win64 and wchar_t is 16 bits, so the Win types below are fixed width
// and the wide string shims never call glibc's wcs functions
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MSABI __attribute__( ( ms_abi ) )

#ifdef __cplusplus
	#define SHIM_STATIC_ASSERT( cond, msg ) static_assert( cond, msg )
extern "C" {
#else
	#define SHIM_STATIC_ASSERT( cond, msg ) _Static_assert( cond, msg )
#endif

typedef int32_t BOOL;
typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef uint64_t SIZE_T;
typedef int64_t SSIZE_T;
typedef uint32_t UINT;
typedef int32_t INT;
typedef int32_t HRESULT;
typedef void* HANDLE;
typedef void* HMODULE;
typedef void* HKEY;
typedef void* LPVOID;
typedef const void* LPCVOID;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef uint16_t WCHAR;
typedef WCHAR* LPWSTR;
typedef const WCHAR* LPCWSTR;
typedef DWORD* LPDWORD;
typedef BOOL* LPBOOL;
typedef void* FARPROC;

#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ( (HANDLE)(intptr_t)-1 )
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFu

// Win32 error codes the shims report through SetLastError
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_PARAMETER 87
#define ERROR_INSUFFICIENT_BUFFER 122
#define ERROR_MOD_NOT_FOUND 126
#define ERROR_PROC_NOT_FOUND 127
#define ERROR_ALREADY_EXISTS 183
#define ERROR_ENVVAR_NOT_FOUND 203
#define ERROR_NO_UNICODE_TRANSLATION 1113

typedef struct LARGE_INTEGER
{
	int64_t QuadPart;
} LARGE_INTEGER;

typedef struct FILETIME
{
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME;

// RTL_CRITICAL_SECTION, 40 bytes on x64.
// The DLL only hands the struct back to the four Enter/Leave/Initialize/Delete imports and never looks inside,
// so LockSemaphore holds our pthread mutex
typedef struct CRITICAL_SECTION
{
	void* DebugInfo;
	LONG LockCount;
	LONG RecursionCount;
	HANDLE OwningThread;
	HANDLE LockSemaphore;
	uintptr_t SpinCount;
} CRITICAL_SECTION;

// The thread environment block, the part of it x64 code addresses through the GS segment.
// Offsets are Windows' own, which is what matters, since compiler-generated code reads some of these directly,
// the Self pointer at 0x30 and LastErrorValue at 0x68 for example, bypassing the kernel32 imports.
// Everything not listed is zero and stays zero
typedef struct shim_teb
{
	void* ExceptionList;              // 0x00
	void* StackBase;                  // 0x08
	void* StackLimit;                 // 0x10
	void* SubSystemTib;               // 0x18
	void* FiberData;                  // 0x20
	void* ArbitraryUserPointer;       // 0x28
	struct shim_teb* Self;            // 0x30
	void* EnvironmentPointer;         // 0x38
	void* UniqueProcess;              // 0x40
	void* UniqueThread;               // 0x48
	void* ActiveRpcHandle;            // 0x50
	void* ThreadLocalStoragePointer;  // 0x58
	void* ProcessEnvironmentBlock;    // 0x60
	DWORD LastErrorValue;             // 0x68
	DWORD CountOfOwnedCriticalSections; // 0x6C
	uint8_t pad0[0x1480 - 0x70];
	void* TlsSlots[64];               // 0x1480
	uint8_t pad1[0x1780 - 0x1680];
	void* TlsExpansionSlots;          // 0x1780
	uint8_t pad2[0x1838 - 0x1788];
} shim_teb;

SHIM_STATIC_ASSERT( offsetof( shim_teb, Self ) == 0x30, "TEB Self offset" );
SHIM_STATIC_ASSERT( offsetof( shim_teb, ProcessEnvironmentBlock ) == 0x60, "TEB PEB offset" );
SHIM_STATIC_ASSERT( offsetof( shim_teb, LastErrorValue ) == 0x68, "TEB LastErrorValue offset" );
SHIM_STATIC_ASSERT( offsetof( shim_teb, TlsSlots ) == 0x1480, "TEB TlsSlots offset" );
SHIM_STATIC_ASSERT( offsetof( shim_teb, TlsExpansionSlots ) == 0x1780, "TEB TlsExpansionSlots offset" );
SHIM_STATIC_ASSERT( sizeof( shim_teb ) == 0x1838, "TEB size" );

// One import the loader can satisfy, matched by DLL name (case-insensitive) and symbol name
typedef struct shim_entry
{
	const char* dll;
	const char* name;
	void* fn;
} shim_entry;

// The three tables, one per shim file, terminated by an entry with a null name
extern const shim_entry shim_kernel32_table[];
extern const shim_entry shim_msvcrt_table[];
extern const shim_entry shim_advapi32_table[];

// The calling thread's TEB, made and installed in GS on first use, see shim_thread_attach
shim_teb* shim_current_teb( void );
void shim_thread_attach( void );

// The handle the DLL was given for itself, and the module's export lookup, both provided by the loader
HMODULE shim_dll_module( void );
void* shim_dll_export( const char* name );
const char* shim_dll_path( void );

// Diagnostics, on when D3DCOMPILER_SHIM_TRACE is set in the environment
extern bool shim_trace_enabled;
void shim_trace( const char* fmt, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
void shim_fatal( const char* fmt, ... ) __attribute__( ( format( printf, 1, 2 ), noreturn ) );

// Length of a 16-bit string, and conversions between UTF-16 and UTF-8, shared by the kernel32 and CRT shims.
// Both return the number of code units written, or needed when the destination is null
size_t shim_wcslen( const WCHAR* s );
size_t shim_utf16_to_utf8( const WCHAR* src, size_t srcLen, char* dst, size_t dstLen );
size_t shim_utf8_to_utf16( const char* src, size_t srcLen, WCHAR* dst, size_t dstLen );

// A file HANDLE is the POSIX descriptor plus this bias, shared by CreateFile and the CRT's _get_osfhandle,
// so no valid handle is 0 or -1
#define SHIM_HANDLE_BIAS 0x400

#define SHIM_UNUSED( x ) (void)( x )

#ifdef __cplusplus
}
#endif
