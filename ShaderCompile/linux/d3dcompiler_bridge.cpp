// D3DCompile on Linux, the loaded DLL's export behind the declaration in d3dcompiler_linux.h
#include "../d3dcompiler_linux.h"
#include "d3dcompiler_blob.h"
#include "peloader.h"
#include "shim.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace
{
	typedef HRESULT( MSABI* PFN_D3DCompile )( LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob** );

	PFN_D3DCompile g_compile;

	std::string exe_path()
	{
		char exe[4096];
		ssize_t n = readlink( "/proc/self/exe", exe, sizeof( exe ) - 1 );
		if ( n <= 0 )
			return "ShaderCompile";
		exe[n] = 0;
		return exe;
	}
} // namespace

bool D3DCompilerLinux_Load( const char* pathOrNull, char* err, size_t errLen )
{
	if ( g_compile )
		return true;
	// The DLL is in the binary, a path on the command line or in the environment swaps in another copy,
	// which is only for trying a different build of it, see the pin in d3dcompiler/SHA256SUMS
	const char* env = getenv( "D3DCOMPILER_47_DLL" );
	const char* path = pathOrNull && *pathOrNull ? pathOrNull : ( env && *env ? env : nullptr );
	pe_module* mod = path ? pe_load( path, err, errLen )
						  : pe_load_image( d3dcompiler_blob, d3dcompiler_blob_size, exe_path().c_str(), err, errLen );
	if ( !mod )
		return false;
	g_compile = (PFN_D3DCompile)pe_get_export( mod, "D3DCompile" );
	if ( !g_compile )
	{
		snprintf( err, errLen, "%s has no D3DCompile export", pe_module_path( mod ) );
		return false;
	}
	return true;
}

HRESULT D3DCompile( LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
	LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs )
{
	if ( !g_compile )
	{
		char err[512];
		if ( !D3DCompilerLinux_Load( nullptr, err, sizeof( err ) ) )
		{
			fprintf( stderr, "ShaderCompile: %s\n", err );
			return E_FAIL;
		}
	}
	// Every worker thread needs its own TEB before the DLL runs on it
	shim_thread_attach();
	return g_compile( pSrcData, SrcDataSize, pSourceName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2, ppCode, ppErrorMsgs );
}
