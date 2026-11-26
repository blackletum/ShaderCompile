// advapi32 and rpcrt4 as d3dcompiler_47.dll uses them, see shim.h for the ground rules.
//
// The registry calls probe Software\Microsoft\Direct3D\Direct3D12 for debug settings and are answered "not found",
// which is what a clean Windows machine says too.
// The Crypt functions are left to the loader's traps, nothing in a shader model 2 or 3 compile hashes
#define _GNU_SOURCE
#include "shim.h"

#include <fcntl.h>
#include <unistd.h>

#define ADV( ret, name ) static MSABI ret adv_##name

ADV( LONG, RegOpenKeyExA )( HKEY key, LPCSTR sub, DWORD options, DWORD access, HKEY* result )
{
	SHIM_UNUSED( key );
	SHIM_UNUSED( options );
	SHIM_UNUSED( access );
	shim_trace( "RegOpenKeyExA(%s) -> not found", sub ? sub : "(null)" );
	if ( result )
		*result = NULL;
	return ERROR_FILE_NOT_FOUND;
}

ADV( LONG, RegOpenKeyExW )( HKEY key, LPCWSTR sub, DWORD options, DWORD access, HKEY* result )
{
	SHIM_UNUSED( key );
	SHIM_UNUSED( sub );
	SHIM_UNUSED( options );
	SHIM_UNUSED( access );
	if ( result )
		*result = NULL;
	return ERROR_FILE_NOT_FOUND;
}

ADV( LONG, RegQueryValueExA )( HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, BYTE* data, LPDWORD size )
{
	SHIM_UNUSED( key );
	SHIM_UNUSED( name );
	SHIM_UNUSED( reserved );
	SHIM_UNUSED( type );
	SHIM_UNUSED( data );
	SHIM_UNUSED( size );
	return ERROR_FILE_NOT_FOUND;
}

ADV( LONG, RegQueryValueExW )( HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, BYTE* data, LPDWORD size )
{
	SHIM_UNUSED( key );
	SHIM_UNUSED( name );
	SHIM_UNUSED( reserved );
	SHIM_UNUSED( type );
	SHIM_UNUSED( data );
	SHIM_UNUSED( size );
	return ERROR_FILE_NOT_FOUND;
}

ADV( LONG, RegEnumKeyExA )( HKEY key, DWORD index, LPSTR name, LPDWORD nameLen, LPDWORD reserved, LPSTR cls, LPDWORD clsLen, FILETIME* written )
{
	SHIM_UNUSED( key );
	SHIM_UNUSED( index );
	SHIM_UNUSED( name );
	SHIM_UNUSED( nameLen );
	SHIM_UNUSED( reserved );
	SHIM_UNUSED( cls );
	SHIM_UNUSED( clsLen );
	SHIM_UNUSED( written );
	return 259; // ERROR_NO_MORE_ITEMS
}

ADV( LONG, RegCloseKey )( HKEY key )
{
	SHIM_UNUSED( key );
	return ERROR_SUCCESS;
}

// A version 4 UUID from the kernel's random pool
static MSABI LONG rpc_UuidCreate( BYTE* uuid )
{
	int fd = open( "/dev/urandom", O_RDONLY | O_CLOEXEC );
	if ( fd < 0 || read( fd, uuid, 16 ) != 16 )
	{
		if ( fd >= 0 )
			close( fd );
		return 1739; // RPC_S_UUID_NO_ADDRESS
	}
	close( fd );
	uuid[7] = (BYTE)( ( uuid[7] & 0x0F ) | 0x40 );
	uuid[8] = (BYTE)( ( uuid[8] & 0x3F ) | 0x80 );
	return 0; // RPC_S_OK
}

const shim_entry shim_advapi32_table[] = {
	{ "ADVAPI32.dll", "RegCloseKey", (void*)adv_RegCloseKey },
	{ "ADVAPI32.dll", "RegEnumKeyExA", (void*)adv_RegEnumKeyExA },
	{ "ADVAPI32.dll", "RegOpenKeyExA", (void*)adv_RegOpenKeyExA },
	{ "ADVAPI32.dll", "RegOpenKeyExW", (void*)adv_RegOpenKeyExW },
	{ "ADVAPI32.dll", "RegQueryValueExA", (void*)adv_RegQueryValueExA },
	{ "ADVAPI32.dll", "RegQueryValueExW", (void*)adv_RegQueryValueExW },
	{ "RPCRT4.dll", "UuidCreate", (void*)rpc_UuidCreate },
	{ NULL, NULL, NULL },
};
