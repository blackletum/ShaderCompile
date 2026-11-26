# ShaderCompile, natively on Linux

The standalone Source shader compiler from the sibling `../ShaderCompile/` fork, built as a Linux binary that
carries Microsoft's own `d3dcompiler_47.dll` inside it and runs it in process, no wine involved,
see `ShaderCompile/linux/peloader.h` for how.
The output is byte for byte what the Windows build produces from the same DLL.

This directory is the fork plus the Linux port and nothing else.
`diff -r --exclude='build-*' ../ShaderCompile .` is the port.
The sibling stays as it was for the mingw and wine route, so a fix to the compiler itself goes into both.
The files the Linux build does not read are not here,
the mingw toolchain, the Source SDK scripts and the unused half of the LZMA SDK among them.
Build the `.exe` from the sibling, build the Linux binary from here.
It is vendored as plain files, `shared/re2` and `shared/gsl` are the contents of the sibling's submodules
at the revisions it had checked out, not submodules themselves.

## Build
```
./build-linux.sh                 # needs cmake, ninja, gcc 15 or newer, g++
build-linux/ShaderCompile -ver 30 -shaderpath src_dir shader.fxc
```

The binary is the whole compiler.
The DLL is committed at `d3dcompiler/d3dcompiler_47.dll`, version 10.0.15063.674, the file winetricks installs,
and cmake compiles it in with C23 `#embed` after checking it against the `SHA256SUMS` beside it,
`fetch.sh` there is the record of where it came from.
THE TRAP: a different build of the DLL is a different compiler and changes every shader's bytecode,
so the hash is the contract, and swapping the file means redoing the pin and every shader on purpose.
The root `.dockerignore` drops every binary under `third_party/` except that one file, named in full.
`-d3dcompiler path` or `$D3DCOMPILER_47_DLL` runs another copy of the DLL instead, for trying a different build of it.
`D3DCOMPILER_SHIM_TRACE=1` logs every call the DLL makes into the loader's shims.
An import the loader does not implement aborts with the symbol's name, add it to `ShaderCompile/linux/shim_*.c`.
The loader does not dispatch exceptions, the compiler never raises one in a compile,
and a C++ throw inside the DLL aborts the process instead of corrupting it.

## Usage
```
ShaderCompile [OPTIONS] -ver n -shaderpath src_dir shader.fxc
```
## Options
```
-ver ARG                       Sets shader version, required
-shaderpath ARG                Base path for shaders, required
-d3dcompiler ARG               Use this d3dcompiler_47.dll instead of the built-in one
-crc                           Calculate crc for shader
-dynamic                       Generate only header
-force                         Skip crc check during compilation
-threads ARG                   Number of threads used, defaults to core count

-h, -help                      Shows help
-verbose                       Verbose file cache and final shader info
-verbose2                      Verbose compile commands
-verbose_preprocessor          Enables preprocessor debug printing

-disable-optimization, /Od     Disables shader optimization
-disable-preshader, /Op        Disables preshader generation
-no-flow-control, /Gfa         Directs the compiler to not use flow-control constructs where possible
-prefer-flow-control, /Gfp     Directs the compiler to use flow-control constructs where possible
-partial-precision, /Gpp       Compiles shader with partial precision
-no-validation, /Vd            Skips shader validation
```
## Shader model version support
All shader models starting from PS2.b/VS2.0

Valid options for `-ver`
```
20          ps2b/vs20
30          ps30/vs30
```
