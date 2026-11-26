// The PE32+ on-disk structures the loader reads, laid out as the PE/COFF specification gives them.
// Only what a 64-bit DLL with imports, exports and base relocations needs, nothing from windows.h
#pragma once

#include <stdint.h>

#define PE_DOS_MAGIC 0x5A4D       // "MZ"
#define PE_NT_SIGNATURE 0x00004550 // "PE\0\0"
#define PE_MACHINE_AMD64 0x8664
#define PE_OPTIONAL_MAGIC_PE32PLUS 0x20B

#define PE_DIR_EXPORT 0
#define PE_DIR_IMPORT 1
#define PE_DIR_BASERELOC 5
#define PE_DIR_TLS 9
#define PE_DIR_DELAY_IMPORT 13
#define PE_NUM_DIRS 16

#define PE_SCN_MEM_EXECUTE 0x20000000u
#define PE_SCN_MEM_READ 0x40000000u
#define PE_SCN_MEM_WRITE 0x80000000u
#define PE_SCN_CNT_UNINITIALIZED_DATA 0x00000080u

#define PE_REL_BASED_ABSOLUTE 0
#define PE_REL_BASED_DIR64 10

#define PE_ORDINAL_FLAG64 0x8000000000000000ull

#define PE_DLL_PROCESS_ATTACH 1
#define PE_DLL_THREAD_ATTACH 2
#define PE_DLL_THREAD_DETACH 3
#define PE_DLL_PROCESS_DETACH 0

#pragma pack( push, 1 )

typedef struct pe_dos_header
{
	uint16_t e_magic;
	uint8_t pad[58];
	int32_t e_lfanew;
} pe_dos_header;

typedef struct pe_file_header
{
	uint16_t Machine;
	uint16_t NumberOfSections;
	uint32_t TimeDateStamp;
	uint32_t PointerToSymbolTable;
	uint32_t NumberOfSymbols;
	uint16_t SizeOfOptionalHeader;
	uint16_t Characteristics;
} pe_file_header;

typedef struct pe_data_directory
{
	uint32_t VirtualAddress;
	uint32_t Size;
} pe_data_directory;

typedef struct pe_optional_header64
{
	uint16_t Magic;
	uint8_t MajorLinkerVersion;
	uint8_t MinorLinkerVersion;
	uint32_t SizeOfCode;
	uint32_t SizeOfInitializedData;
	uint32_t SizeOfUninitializedData;
	uint32_t AddressOfEntryPoint;
	uint32_t BaseOfCode;
	uint64_t ImageBase;
	uint32_t SectionAlignment;
	uint32_t FileAlignment;
	uint16_t MajorOperatingSystemVersion;
	uint16_t MinorOperatingSystemVersion;
	uint16_t MajorImageVersion;
	uint16_t MinorImageVersion;
	uint16_t MajorSubsystemVersion;
	uint16_t MinorSubsystemVersion;
	uint32_t Win32VersionValue;
	uint32_t SizeOfImage;
	uint32_t SizeOfHeaders;
	uint32_t CheckSum;
	uint16_t Subsystem;
	uint16_t DllCharacteristics;
	uint64_t SizeOfStackReserve;
	uint64_t SizeOfStackCommit;
	uint64_t SizeOfHeapReserve;
	uint64_t SizeOfHeapCommit;
	uint32_t LoaderFlags;
	uint32_t NumberOfRvaAndSizes;
	pe_data_directory DataDirectory[PE_NUM_DIRS];
} pe_optional_header64;

typedef struct pe_nt_headers64
{
	uint32_t Signature;
	pe_file_header FileHeader;
	pe_optional_header64 OptionalHeader;
} pe_nt_headers64;

typedef struct pe_section_header
{
	char Name[8];
	uint32_t VirtualSize;
	uint32_t VirtualAddress;
	uint32_t SizeOfRawData;
	uint32_t PointerToRawData;
	uint32_t PointerToRelocations;
	uint32_t PointerToLinenumbers;
	uint16_t NumberOfRelocations;
	uint16_t NumberOfLinenumbers;
	uint32_t Characteristics;
} pe_section_header;

typedef struct pe_import_descriptor
{
	uint32_t OriginalFirstThunk; // RVA of the name table, zero in the terminating entry
	uint32_t TimeDateStamp;
	uint32_t ForwarderChain;
	uint32_t Name;       // RVA of the DLL name
	uint32_t FirstThunk; // RVA of the import address table the code calls through
} pe_import_descriptor;

typedef struct pe_import_by_name
{
	uint16_t Hint;
	char Name[1];
} pe_import_by_name;

typedef struct pe_base_relocation
{
	uint32_t VirtualAddress;
	uint32_t SizeOfBlock;
} pe_base_relocation;

typedef struct pe_export_directory
{
	uint32_t Characteristics;
	uint32_t TimeDateStamp;
	uint16_t MajorVersion;
	uint16_t MinorVersion;
	uint32_t Name;
	uint32_t Base;
	uint32_t NumberOfFunctions;
	uint32_t NumberOfNames;
	uint32_t AddressOfFunctions;
	uint32_t AddressOfNames;
	uint32_t AddressOfNameOrdinals;
} pe_export_directory;

#pragma pack( pop )
