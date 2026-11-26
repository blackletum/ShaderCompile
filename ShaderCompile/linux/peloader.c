// See peloader.h
#define _GNU_SOURCE
#include "peloader.h"
#include "pe_types.h"
#include "shim.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct pe_module
{
	uint8_t* base;
	size_t size;
	char* path;
	const pe_nt_headers64* nt;
	const pe_section_header* sections;
};

static pe_module* g_module;

HMODULE shim_dll_module( void )
{
	return g_module ? g_module->base : NULL;
}

void* shim_dll_export( const char* name )
{
	return g_module ? pe_get_export( g_module, name ) : NULL;
}

const char* shim_dll_path( void )
{
	return g_module ? g_module->path : "";
}

static void fail( char* err, size_t errLen, const char* fmt, ... ) __attribute__( ( format( printf, 3, 4 ) ) );
static void fail( char* err, size_t errLen, const char* fmt, ... )
{
	if ( !err || !errLen )
		return;
	va_list ap;
	va_start( ap, fmt );
	vsnprintf( err, errLen, fmt, ap );
	va_end( ap );
}

static const void* rva( const pe_module* m, uint32_t off )
{
	return m->base + off;
}

static bool read_file( const char* path, uint8_t** data, size_t* size, char* err, size_t errLen )
{
	int fd = open( path, O_RDONLY | O_CLOEXEC );
	if ( fd < 0 )
	{
		fail( err, errLen, "cannot open %s: %s", path, strerror( errno ) );
		return false;
	}
	struct stat st;
	if ( fstat( fd, &st ) < 0 || st.st_size <= 0 )
	{
		fail( err, errLen, "cannot stat %s", path );
		close( fd );
		return false;
	}
	uint8_t* buf = malloc( (size_t)st.st_size );
	size_t got = 0;
	while ( got < (size_t)st.st_size )
	{
		ssize_t n = read( fd, buf + got, (size_t)st.st_size - got );
		if ( n <= 0 )
		{
			fail( err, errLen, "short read on %s", path );
			close( fd );
			free( buf );
			return false;
		}
		got += (size_t)n;
	}
	close( fd );
	*data = buf;
	*size = got;
	return true;
}

// The import trampolines.
// An unknown import gets 32 bytes of code that loads its name and jumps to the reporter,
// which never returns, so the convention mismatch between the DLL's call and our C does not matter.
// One RWX page holds 128 of them, the DLL has 198 imports and most have shims
static uint8_t* trap_page;
static size_t trap_used;
static const size_t TRAP_SIZE = 32;

static void __attribute__( ( noreturn, used ) ) unknown_import_called( const char* name )
{
	fprintf( stderr, "\nShaderCompile: d3dcompiler_47.dll called an import this loader does not implement: %s\n"
					 "Add it to the shim tables in ShaderCompile/linux and rebuild.\n",
		name );
	abort();
}

static void* make_trap( const char* dll, const char* name )
{
	if ( !trap_page || trap_used + TRAP_SIZE > 4096 )
	{
		trap_page = mmap( NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
		if ( trap_page == MAP_FAILED )
			shim_fatal( "mmap for import trampolines failed: %s", strerror( errno ) );
		trap_used = 0;
	}
	char* label = malloc( strlen( dll ) + strlen( name ) + 2 );
	sprintf( label, "%s!%s", dll, name );

	uint8_t* code = trap_page + trap_used;
	trap_used += TRAP_SIZE;
	uint8_t* p = code;
	// movabs rdi, label
	*p++ = 0x48; *p++ = 0xBF;
	memcpy( p, &label, 8 ); p += 8;
	// movabs rax, unknown_import_called
	void* target = (void*)unknown_import_called;
	*p++ = 0x48; *p++ = 0xB8;
	memcpy( p, &target, 8 ); p += 8;
	// jmp rax
	*p++ = 0xFF; *p++ = 0xE0;
	return code;
}

static void* lookup_shim( const char* dll, const char* name )
{
	const shim_entry* tables[] = { shim_kernel32_table, shim_msvcrt_table, shim_advapi32_table };
	for ( size_t t = 0; t < sizeof( tables ) / sizeof( tables[0] ); ++t )
		for ( const shim_entry* e = tables[t]; e->name; ++e )
			if ( !strcasecmp( e->dll, dll ) && !strcmp( e->name, name ) )
				return e->fn;
	return NULL;
}

// The image was linked for ImageBase and we mapped it wherever mmap put it,
// so every absolute address in it moves by the difference.
// The relocation directory is a list of 4 KB page blocks, each a list of 16-bit entries, type in the top nibble
// and page offset in the rest, and a 64-bit DLL only uses DIR64, the whole 8-byte slot
static bool apply_relocations( pe_module* m, char* err, size_t errLen )
{
	const pe_data_directory* dir = &m->nt->OptionalHeader.DataDirectory[PE_DIR_BASERELOC];
	int64_t delta = (int64_t)( (uintptr_t)m->base - m->nt->OptionalHeader.ImageBase );
	if ( delta == 0 || !dir->Size )
		return true;

	const uint8_t* p = rva( m, dir->VirtualAddress );
	const uint8_t* end = p + dir->Size;
	while ( p + sizeof( pe_base_relocation ) <= end )
	{
		const pe_base_relocation* block = (const pe_base_relocation*)p;
		if ( block->SizeOfBlock < sizeof( pe_base_relocation ) )
			break;
		const uint16_t* entries = (const uint16_t*)( p + sizeof( pe_base_relocation ) );
		size_t count = ( block->SizeOfBlock - sizeof( pe_base_relocation ) ) / 2;
		for ( size_t i = 0; i < count; ++i )
		{
			unsigned type = entries[i] >> 12;
			unsigned off = entries[i] & 0xFFF;
			if ( type == PE_REL_BASED_ABSOLUTE )
				continue;
			if ( type != PE_REL_BASED_DIR64 )
			{
				fail( err, errLen, "unsupported relocation type %u", type );
				return false;
			}
			uint64_t* slot = (uint64_t*)( m->base + block->VirtualAddress + off );
			*slot = (uint64_t)( (int64_t)*slot + delta );
		}
		p += block->SizeOfBlock;
	}
	return true;
}

// One descriptor per imported DLL, each with a name table and an address table of the same length.
// The DLL's code calls through the address table, so writing our function there is the whole trick,
// the same thing the Windows loader does with the real kernel32
static bool resolve_imports( pe_module* m, char* err, size_t errLen )
{
	const pe_data_directory* dir = &m->nt->OptionalHeader.DataDirectory[PE_DIR_IMPORT];
	if ( !dir->Size )
		return true;
	unsigned shimmed = 0, trapped = 0;
	for ( const pe_import_descriptor* d = rva( m, dir->VirtualAddress ); d->Name; ++d )
	{
		const char* dll = rva( m, d->Name );
		const uint64_t* names = rva( m, d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk );
		uint64_t* iat = (uint64_t*)rva( m, d->FirstThunk );
		for ( ; *names; ++names, ++iat )
		{
			if ( *names & PE_ORDINAL_FLAG64 )
			{
				fail( err, errLen, "%s imports by ordinal %llu, not supported", dll, (unsigned long long)( *names & 0xFFFF ) );
				return false;
			}
			const pe_import_by_name* ibn = rva( m, (uint32_t)*names );
			void* fn = lookup_shim( dll, ibn->Name );
			if ( fn )
				++shimmed;
			else
			{
				fn = make_trap( dll, ibn->Name );
				++trapped;
				shim_trace( "import %s!%s has no shim, trapped", dll, ibn->Name );
			}
			*iat = (uint64_t)(uintptr_t)fn;
		}
	}
	shim_trace( "imports resolved, %u shimmed, %u trapped", shimmed, trapped );
	return true;
}

static int section_prot( uint32_t ch )
{
	int prot = 0;
	if ( ch & PE_SCN_MEM_READ )
		prot |= PROT_READ;
	if ( ch & PE_SCN_MEM_WRITE )
		prot |= PROT_WRITE;
	if ( ch & PE_SCN_MEM_EXECUTE )
		prot |= PROT_EXEC;
	return prot;
}

static bool protect_sections( pe_module* m, char* err, size_t errLen )
{
	// Headers stay readable, the DLL's own code reads them through GetModuleHandle for its resource section
	if ( mprotect( m->base, m->nt->OptionalHeader.SizeOfHeaders, PROT_READ ) )
	{
		fail( err, errLen, "mprotect headers: %s", strerror( errno ) );
		return false;
	}
	for ( unsigned i = 0; i < m->nt->FileHeader.NumberOfSections; ++i )
	{
		const pe_section_header* s = &m->sections[i];
		size_t size = s->VirtualSize ? s->VirtualSize : s->SizeOfRawData;
		if ( !size )
			continue;
		size = ( size + 4095 ) & ~(size_t)4095;
		if ( mprotect( m->base + s->VirtualAddress, size, section_prot( s->Characteristics ) ) )
		{
			fail( err, errLen, "mprotect section %.8s: %s", s->Name, strerror( errno ) );
			return false;
		}
	}
	return true;
}

typedef BOOL( MSABI* pfn_dllmain )( HMODULE, DWORD, void* );

pe_module* pe_load_image( const void* image, size_t fileSize, const char* name, char* err, size_t errLen )
{
	if ( g_module )
	{
		fail( err, errLen, "only one module can be loaded" );
		return NULL;
	}
	const uint8_t* file = image;
	const pe_dos_header* dos = (const pe_dos_header*)file;
	if ( fileSize < sizeof( pe_dos_header ) || dos->e_magic != PE_DOS_MAGIC || dos->e_lfanew <= 0
		|| (size_t)dos->e_lfanew + sizeof( pe_nt_headers64 ) > fileSize )
	{
		fail( err, errLen, "%s is not a PE file", name );
		return NULL;
	}
	const pe_nt_headers64* nt = (const pe_nt_headers64*)( file + dos->e_lfanew );
	if ( nt->Signature != PE_NT_SIGNATURE || nt->FileHeader.Machine != PE_MACHINE_AMD64
		|| nt->OptionalHeader.Magic != PE_OPTIONAL_MAGIC_PE32PLUS )
	{
		fail( err, errLen, "%s is not a 64-bit x86-64 PE image", name );
		return NULL;
	}
	if ( nt->OptionalHeader.DataDirectory[PE_DIR_TLS].Size || nt->OptionalHeader.DataDirectory[PE_DIR_DELAY_IMPORT].Size )
	{
		fail( err, errLen, "%s has a TLS or delay-import directory, which this loader does not handle", name );
		return NULL;
	}

	// The image, reserved as one RW mapping, sections copied in, then protected per section.
	// Any base works, the DLL is relocatable and we relocate it, so no fixed address is asked for
	size_t imageSize = ( nt->OptionalHeader.SizeOfImage + 0xFFFF ) & ~(size_t)0xFFFF;
	uint8_t* base = mmap( NULL, imageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
	if ( base == MAP_FAILED )
	{
		fail( err, errLen, "mmap of %zu bytes for the image failed: %s", imageSize, strerror( errno ) );
		return NULL;
	}
	memcpy( base, file, nt->OptionalHeader.SizeOfHeaders );
	const pe_section_header* sections = (const pe_section_header*)( (const uint8_t*)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader );
	for ( unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i )
	{
		const pe_section_header* s = &sections[i];
		size_t raw = s->SizeOfRawData;
		if ( s->VirtualSize && raw > s->VirtualSize )
			raw = s->VirtualSize;
		if ( raw && s->PointerToRawData + raw <= fileSize )
			memcpy( base + s->VirtualAddress, file + s->PointerToRawData, raw );
	}

	pe_module* m = calloc( 1, sizeof( *m ) );
	m->base = base;
	m->size = imageSize;
	m->path = strdup( name );
	m->nt = (const pe_nt_headers64*)( base + dos->e_lfanew );
	m->sections = (const pe_section_header*)( (const uint8_t*)&m->nt->OptionalHeader + m->nt->FileHeader.SizeOfOptionalHeader );

	if ( !apply_relocations( m, err, errLen ) || !resolve_imports( m, err, errLen ) || !protect_sections( m, err, errLen ) )
	{
		munmap( base, imageSize );
		free( m->path );
		free( m );
		return NULL;
	}
	g_module = m;

	// The entry point is _DllMainCRTStartup, which runs the CRT's static initializers through _initterm
	// before anything else can be called
	shim_thread_attach();
	if ( m->nt->OptionalHeader.AddressOfEntryPoint )
	{
		pfn_dllmain entry = (pfn_dllmain)( base + m->nt->OptionalHeader.AddressOfEntryPoint );
		shim_trace( "calling DllMain" );
		if ( !entry( base, PE_DLL_PROCESS_ATTACH, NULL ) )
		{
			fail( err, errLen, "DllMain of %s returned FALSE", name );
			g_module = NULL;
			munmap( base, imageSize );
			free( m->path );
			free( m );
			return NULL;
		}
	}
	return m;
}

pe_module* pe_load( const char* path, char* err, size_t errLen )
{
	uint8_t* file = NULL;
	size_t fileSize = 0;
	if ( !read_file( path, &file, &fileSize, err, errLen ) )
		return NULL;
	char* full = realpath( path, NULL );
	pe_module* m = pe_load_image( file, fileSize, full ? full : path, err, errLen );
	free( full );
	free( file );
	return m;
}

void* pe_get_export( const pe_module* m, const char* name )
{
	const pe_data_directory* dir = &m->nt->OptionalHeader.DataDirectory[PE_DIR_EXPORT];
	if ( !dir->Size )
		return NULL;
	const pe_export_directory* ex = rva( m, dir->VirtualAddress );
	const uint32_t* names = rva( m, ex->AddressOfNames );
	const uint16_t* ordinals = rva( m, ex->AddressOfNameOrdinals );
	const uint32_t* functions = rva( m, ex->AddressOfFunctions );
	for ( uint32_t i = 0; i < ex->NumberOfNames; ++i )
	{
		if ( !strcmp( rva( m, names[i] ), name ) )
		{
			uint32_t fnRva = functions[ordinals[i]];
			// A forwarded export points inside the export directory itself, this DLL has none
			if ( fnRva >= dir->VirtualAddress && fnRva < dir->VirtualAddress + dir->Size )
				return NULL;
			return (void*)rva( m, fnRva );
		}
	}
	return NULL;
}

void* pe_module_base( const pe_module* m )
{
	return m->base;
}

const char* pe_module_path( const pe_module* m )
{
	return m->path;
}
