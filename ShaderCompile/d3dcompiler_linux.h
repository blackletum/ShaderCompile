// d3dcompiler.h for the Linux build, the part of it ShaderCompile uses, with the real d3dcompiler_47.dll behind it.
//
// The DLL is loaded by linux/peloader.c and D3DCompile below forwards to its export,
// see linux/d3dcompiler_bridge.cpp.
// THE TRAP: the COM interfaces are plain C++ classes with virtual methods, which works because the Itanium vtable
// puts the methods in declaration order from slot 0 exactly like MSVC does, but only with every method marked
// MSABI, the DLL calls Open and Release with the Windows convention and reads this out of RCX.
// A class deriving from these may add its own virtuals (a virtual destructor included) after the interface's,
// never before
#pragma once

#include <cstddef>
#include <cstdint>

#define MSABI __attribute__( ( ms_abi ) )

typedef int32_t HRESULT;
typedef uint32_t UINT;
typedef uint32_t ULONG;
typedef uint64_t SIZE_T;
typedef const char* LPCSTR;
typedef void* LPVOID;
typedef const void* LPCVOID;

#define S_OK ( (HRESULT)0 )
#define E_FAIL ( (HRESULT)0x80004005 )
#define E_OUTOFMEMORY ( (HRESULT)0x8007000E )
#define FAILED( hr ) ( ( (HRESULT)( hr ) ) < 0 )
#define SUCCEEDED( hr ) ( ( (HRESULT)( hr ) ) >= 0 )

#define STDMETHODCALLTYPE MSABI
#define STDMETHOD( method ) virtual HRESULT MSABI method
#define THIS_

#define D3DCOMPILE_DEBUG 0x00000001
#define D3DCOMPILE_SKIP_VALIDATION 0x00000002
#define D3DCOMPILE_SKIP_OPTIMIZATION 0x00000004
#define D3DCOMPILE_PACK_MATRIX_ROW_MAJOR 0x00000008
#define D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR 0x00000010
#define D3DCOMPILE_PARTIAL_PRECISION 0x00000020
#define D3DCOMPILE_FORCE_VS_SOFTWARE_NO_OPT 0x00000040
#define D3DCOMPILE_FORCE_PS_SOFTWARE_NO_OPT 0x00000080
#define D3DCOMPILE_NO_PRESHADER 0x00000100
#define D3DCOMPILE_AVOID_FLOW_CONTROL 0x00000200
#define D3DCOMPILE_PREFER_FLOW_CONTROL 0x00000400
#define D3DCOMPILE_ENABLE_STRICTNESS 0x00000800
#define D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY 0x00001000
#define D3DCOMPILE_IEEE_STRICTNESS 0x00002000
#define D3DCOMPILE_OPTIMIZATION_LEVEL0 0x00004000
#define D3DCOMPILE_OPTIMIZATION_LEVEL1 0x00000000
#define D3DCOMPILE_OPTIMIZATION_LEVEL2 0x0000c000
#define D3DCOMPILE_OPTIMIZATION_LEVEL3 0x00008000
#define D3DCOMPILE_WARNINGS_ARE_ERRORS 0x00040000
#define D3DCOMPILE_DEBUG_NAME_FOR_SOURCE 0x00400000
#define D3DCOMPILE_DEBUG_NAME_FOR_BINARY 0x00800000

typedef enum D3D_INCLUDE_TYPE
{
	D3D_INCLUDE_LOCAL = 0,
	D3D_INCLUDE_SYSTEM = 1,
} D3D_INCLUDE_TYPE;

struct D3D_SHADER_MACRO
{
	LPCSTR Name;
	LPCSTR Definition;
};

struct GUID
{
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
};

struct ID3DBlob
{
	virtual HRESULT MSABI QueryInterface( const GUID& riid, void** ppv ) = 0;
	virtual ULONG MSABI AddRef() = 0;
	virtual ULONG MSABI Release() = 0;
	virtual LPVOID MSABI GetBufferPointer() = 0;
	virtual SIZE_T MSABI GetBufferSize() = 0;
};

struct ID3DInclude
{
	STDMETHOD( Open )( THIS_ D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData, LPCVOID* ppData, UINT* pBytes ) = 0;
	STDMETHOD( Close )( THIS_ LPCVOID pData ) = 0;
};

// Loads d3dcompiler_47.dll once, the copy compiled into this binary unless a path is given,
// and reports the failure in err
bool D3DCompilerLinux_Load( const char* pathOrNull, char* err, size_t errLen );

// The DLL's D3DCompile, callable the normal way from C++
HRESULT D3DCompile( LPCVOID pSrcData, SIZE_T SrcDataSize, LPCSTR pSourceName, const D3D_SHADER_MACRO* pDefines, ID3DInclude* pInclude,
	LPCSTR pEntrypoint, LPCSTR pTarget, UINT Flags1, UINT Flags2, ID3DBlob** ppCode, ID3DBlob** ppErrorMsgs );
