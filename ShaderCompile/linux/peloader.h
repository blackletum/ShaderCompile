// A loader for one 64-bit Windows DLL inside a Linux process, enough to run d3dcompiler_47.dll natively.
//
// The image is mapped, relocated and its imports are pointed at the shims in this directory,
// then DllMain runs and the exports are callable through MSABI function pointers.
// Imports no shim provides resolve to a trampoline that names the symbol and aborts,
// so a DLL update that starts using something new fails loudly on the first call rather than corrupting state.
//
// This is deliberately not a general loader, no TLS directory, no delay imports, no forwarders, no SEH,
// because d3dcompiler_47 has none of those and traced compiles never dispatch an exception.
// Written against the PE/COFF specification with taviso's loadlibrary and fancycode's MemoryModule as references,
// nothing copied from either
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pe_module pe_module;

// Maps the DLL from its file image in memory, resolves imports, runs DllMain.
// The image is copied, so the bytes can live in .rodata, and name is what the DLL is told its path is.
// Returns null and writes a message into err on failure
pe_module* pe_load_image( const void* image, size_t size, const char* name, char* err, size_t errLen );

// The same from a file on disk
pe_module* pe_load( const char* path, char* err, size_t errLen );

// Address of a named export, null when there is none
void* pe_get_export( const pe_module* mod, const char* name );

// The base address the image was mapped at, what Windows would call the HMODULE
void* pe_module_base( const pe_module* mod );
const char* pe_module_path( const pe_module* mod );

#ifdef __cplusplus
}
#endif
