cmake_minimum_required(VERSION 3.7...3.19)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
    cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

# The native Linux build only, the Windows build lives in the sibling ShaderCompile/ directory.
# d3dcompiler_47.dll is compiled into the binary and run by our own PE loader, see ShaderCompile/linux/peloader.h,
# so nothing here links against it and the binary is the whole compiler
project(ShaderCompile C CXX)

option(RE2_BUILD_TESTING "" OFF)

add_subdirectory(shared/re2)
add_subdirectory(shared/gsl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C23 for #embed in d3dcompiler_blob.c, gnu for the ms_abi attribute and the builtin va_list in the shims
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# The DLL, committed in d3dcompiler/ beside its SHA-256 pin and the script that says where it came from.
# THE TRAP: a different build of the DLL is a different compiler and changes every shader's bytecode,
# so the pin is checked here, before anything is built, and a swapped file stops the build
# on every machine the same way until the pin and every shader have been redone on purpose.
# The root .dockerignore drops every binary under third_party/ except this one file, by name, keep that in step
set(D3DCOMPILER_DLL "${CMAKE_CURRENT_SOURCE_DIR}/d3dcompiler/d3dcompiler_47.dll"
    CACHE FILEPATH "The d3dcompiler_47.dll compiled into the binary, its SHA256SUMS must sit beside it")
if(NOT EXISTS "${D3DCOMPILER_DLL}")
    message(FATAL_ERROR "d3dcompiler_47.dll not found at ${D3DCOMPILER_DLL}, it is committed there")
endif()
get_filename_component(D3DCOMPILER_DIR "${D3DCOMPILER_DLL}" DIRECTORY)
file(STRINGS "${D3DCOMPILER_DIR}/SHA256SUMS" D3DCOMPILER_PIN_LINE REGEX "d3dcompiler_47\\.dll$")
string(REGEX MATCH "^[0-9a-f]+" D3DCOMPILER_PIN "${D3DCOMPILER_PIN_LINE}")
file(SHA256 "${D3DCOMPILER_DLL}" D3DCOMPILER_ACTUAL)
if(NOT D3DCOMPILER_ACTUAL STREQUAL D3DCOMPILER_PIN)
    message(FATAL_ERROR "d3dcompiler_47.dll does not match its pin\n  file: ${D3DCOMPILER_ACTUAL}\n  pin:  ${D3DCOMPILER_PIN}\n"
                        "A different DLL is a different compiler, see ${D3DCOMPILER_DIR}/fetch.sh")
endif()

set(SRC
    ShaderCompile/cfgprocessor.cpp
    ShaderCompile/d3dxfxc.cpp
    ShaderCompile/ShaderCompile.cpp
    ShaderCompile/shaderparser.cpp
    ShaderCompile/utlbuffer.cpp
    ShaderCompile/linux/d3dcompiler_bridge.cpp
)

set(SRC_LOADER
    ShaderCompile/linux/d3dcompiler_blob.c
    ShaderCompile/linux/peloader.c
    ShaderCompile/linux/shim_advapi32.c
    ShaderCompile/linux/shim_common.c
    ShaderCompile/linux/shim_kernel32.c
    ShaderCompile/linux/shim_msvcrt.c
)
set_source_files_properties(${SRC_LOADER} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
# The depfile already tracks the embedded file, OBJECT_DEPENDS is for a generator without depfiles
set_source_files_properties(ShaderCompile/linux/d3dcompiler_blob.c PROPERTIES
    COMPILE_DEFINITIONS "D3DCOMPILER_DLL_PATH=\"${D3DCOMPILER_DLL}\""
    OBJECT_DEPENDS "${D3DCOMPILER_DLL}")

add_executable(ShaderCompile ${SRC} ${SRC_LOADER})
target_link_libraries(ShaderCompile PRIVATE re2::re2 Microsoft.GSL::GSL pthread m)

# Static runtime so the binary moves between images unchanged
target_link_options(ShaderCompile PRIVATE -static-libgcc -static-libstdc++)

target_include_directories(ShaderCompile PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ShaderCompile/include
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/re2
)
