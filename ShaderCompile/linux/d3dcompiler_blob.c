// d3dcompiler_47.dll compiled into the binary with C23 #embed, so the compiler ships as one file.
// CMake passes the path and checks the DLL against its SHA-256 pin before anything is built,
// and the compiler's depfile lists the DLL, so a changed file rebuilds this object like any header would
#include "d3dcompiler_blob.h"

const unsigned char d3dcompiler_blob[] = {
#embed D3DCOMPILER_DLL_PATH
};

const size_t d3dcompiler_blob_size = sizeof d3dcompiler_blob;
