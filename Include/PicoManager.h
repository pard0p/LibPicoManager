/*
 * PICO Manager Library
 * 
 * Manages Position Independent Code Objects (PICOs) in memory.
 * Provides registration, lookup, and tracking of dynamically loaded modules.
 */

#ifndef PICO_MANAGER_H
#define PICO_MANAGER_H

#include <windows.h>

#define PICO_NAME_MAX_LENGTH 32

/* ========================================================================
 * TYPE DEFINITIONS
 * ======================================================================== */

typedef struct {
	__typeof__(LoadLibraryA)   * LoadLibraryA;
	__typeof__(GetProcAddress) * GetProcAddress;
} IMPORTFUNCS;

/*
 * Individual PICO module entry
 * Stores metadata and pointers for a single loaded PICO module
 */
typedef struct _PICO_ENTRY {
    DWORD id;                               /* Unique identifier */
    char name[PICO_NAME_MAX_LENGTH];        /* Module name for lookup */
    char* code;                             /* Pointer to code section in shared RWX block */
    SIZE_T codeSize;                        /* Size of code section */
    char* data;                             /* Pointer to data section in separate RW block */
    SIZE_T dataSize;                        /* Size of data section */
    char* entryPoint;                       /* Module entry point function */
    char* vault;                            /* Pointer to original PICO buffer (read-only reference) */
} PICO_ENTRY, *PPICO_ENTRY;

/*
 * PICO Manager structure
 * Tracks all loaded PICO modules and manages the shared RWX memory block
 */
typedef struct _PICO_MANAGER {
    char* baseAddress;                      /* Base address of shared RWX memory block */
    SIZE_T blockSize;                       /* Total size of RWX memory block */
    SIZE_T usedSize;                        /* Currently used size in bytes */
    PPICO_ENTRY entries;                    /* Array of PICO entries */
    DWORD entryCount;                       /* Number of registered PICOs */
    DWORD entryCapacity;                    /* Maximum capacity of entries array */
    SIZE_T interPicoPadding;                /* Padding between PICOs in bytes */
} PICO_MANAGER, *PPICO_MANAGER;

/* ========================================================================
 * FUNCTION DECLARATIONS
 * ======================================================================== */

/*
 * Initializes the PICO manager structure WITHOUT allocating the RWX block.
 * Only sets up the manager metadata and entry tracking.
 * The caller is responsible for allocating and assigning the RWX block later.
 *
 * @param manager        - Pointer to the PICO_MANAGER structure to initialize
 * @param entries        - Pointer to the array of PICO_ENTRY structures
 * @param entryCapacity  - Maximum number of entries the array can hold
 */
void PicoManagerInit(
    PPICO_MANAGER manager, 
    PPICO_ENTRY entries, 
    DWORD entryCapacity
);

/*
 * Registers a new PICO module in the manager.
 * Only stores metadata and vault reference - does not allocate or load yet.
 * Code and data sizes are extracted from the PICO using LibTcg.
 *
 * @param manager    - Pointer to the PICO_MANAGER structure
 * @param name       - Name of the PICO module (null-terminated string)
 * @param vault      - Pointer to the original PICO buffer
 * @return TRUE on success, FALSE if manager is full or arguments are invalid
 */
BOOL AddPico(
    PPICO_MANAGER manager, 
    const char* name, 
    char* vault
);

/*
 * Allocates the shared RWX memory block for storing PICO code sections.
 * Must be called after adding all initial PICOs or before each load phase.
 * Calculates required size based on registered PICOs and desired padding.
 *
 * @param manager        - Pointer to the PICO_MANAGER structure
 * @param finalPadding   - Additional padding in bytes to reserve at end of block
 * @return TRUE on success, FALSE if allocation failed
 */
BOOL PicoManagerAlloc(
    PPICO_MANAGER manager,
    SIZE_T finalPadding
);

/*
 * Loads all registered but not yet loaded PICOs into the manager's RWX block.
 * Places PICO code sections sequentially in the block with padding.
 * Allocates separate RW blocks for each PICO's data section.
 *
 * @param manager      - Pointer to the PICO_MANAGER structure
 * @param upToEntryId  - Load only entries up to and including this ID (or -1 for all)
 * @param finalPadding - Additional padding in bytes to reserve at the end
 * @param funcs        - Import functions structure for loading
 * @return TRUE on success, FALSE if not enough space or loading failure
 *
 * This function can be called multiple times to load more PICOs in phases,
 * as long as the block was allocated with sufficient size.
 * Example: LoadPico(mgr, 0, 10, funcs) - loads only entry 0 (hooks)
 * Example: LoadPico(mgr, -1, 10, funcs) - loads all entries
 */
BOOL LoadPico(
    PPICO_MANAGER manager,
    DWORD upToEntryId,
    SIZE_T finalPadding,
    IMPORTFUNCS * funcs
);

/*
 * Removes a PICO entry from the manager by its numeric ID.
 * Frees allocated memory and compacts the array.
 * All subsequent entries shift left and their IDs are recalculated.
 * entryCount is decremented.
 *
 * @param manager - Pointer to the PICO_MANAGER structure
 * @param id      - ID of the PICO entry to remove
 * @return TRUE on success, FALSE if ID is invalid
 *
 * Example: [A(id=0), B(id=1), C(id=2)] -> RemovePicoById(mgr, 1) -> [A(id=0), C(id=1)]
 */
BOOL RemovePicoById(
    PPICO_MANAGER manager, 
    DWORD id
);

/*
 * Removes a PICO entry from the manager by its name.
 * Frees allocated memory and compacts the array.
 * All subsequent entries shift left and their IDs are recalculated.
 * entryCount is decremented.
 *
 * @param manager - Pointer to the PICO_MANAGER structure
 * @param name    - Name of the PICO entry to remove
 * @return TRUE on success, FALSE if name is not found
 *
 * Example: [A(id=0), B(id=1), C(id=2)] -> RemovePicoByName(mgr, "B") -> [A(id=0), C(id=1)]
 */
BOOL RemovePicoByName(
    PPICO_MANAGER manager, 
    const char* name
);

/*
 * Retrieves a PICO entry by its numeric ID.
 *
 * @param manager - Pointer to the PICO_MANAGER structure
 * @param id      - Numeric ID of the PICO entry
 * @return Pointer to PICO_ENTRY, or NULL if not found
 */
PPICO_ENTRY GetPicoById(
    PPICO_MANAGER manager, 
    DWORD id
);

/*
 * Retrieves a PICO entry by its name.
 * Performs case-sensitive string comparison.
 *
 * @param manager - Pointer to the PICO_MANAGER structure
 * @param name    - Name of the PICO module (null-terminated string)
 * @return Pointer to PICO_ENTRY, or NULL if not found
 */
PPICO_ENTRY GetPicoByName(
    PPICO_MANAGER manager, 
    const char* name
);

/*
 * Retrieves an export from a PICO module by ID.
 * Uses PicoGetExport from LibTcg to find the export.
 *
 * @param manager - Pointer to the PICO_MANAGER structure
 * @param id      - Numeric ID of the PICO entry
 * @param tag     - Export tag identifier
 * @return Export address as char*, or NULL if not found
 */
char* GetPicoExportById(
    PPICO_MANAGER manager, 
    DWORD id, 
    int tag
);

/*
 * Retrieves an export from a PICO module by name.
 * Uses PicoGetExport from LibTcg to find the export.
 *
 * @param manager - Pointer to the PICO_MANAGER structure
 * @param name    - Name of the PICO module (null-terminated string)
 * @param tag     - Export tag identifier
 * @return Export address as char*, or NULL if not found
 */
char* GetPicoExportByName(
    PPICO_MANAGER manager, 
    const char* name, 
    int tag
);

/*
 * Calculates the total code size required for all registered PICO modules.
 * Includes padding between modules but excludes final padding.
 * Works with both allocated and non-allocated entries.
 *
 * @param manager - Pointer to the PICO_MANAGER structure
 * @return Total size in bytes needed for all PICO code sections with padding
 */
SIZE_T TotalCodeSize(PPICO_MANAGER manager);

/*
 * Duplicates the PICO manager and calculates required memory for all registered PICOs.
 * Creates a new manager with proper sizing, but does NOT allocate the code sections yet.
 * This allows the caller to determine if a new block is needed.
 *
 * @param manager        - Pointer to the source PICO_MANAGER structure
 * @param newManager     - Pointer to the new PICO_MANAGER to initialize
 * @param entries        - Array of PICO_ENTRY structures for the new manager
 * @param entryCapacity  - Maximum capacity of the new entries array
 * @param picoBlock      - Output parameter: address of newly allocated RWX block
 * @return TRUE on success, FALSE on failure (allocation failed or invalid arguments)
 *
 * Note: The new block is allocated here, but PICOs are NOT loaded yet.
 * Call PicoManagerAlloc() on the new manager to load all PICOs into the new block.
 * Vaults are preserved and can be reused. Data sections will be recreated during alloc.
 */
BOOL DuplicateManager(
    PPICO_MANAGER manager,
    PPICO_MANAGER newManager,
    PPICO_ENTRY entries,
    DWORD entryCapacity,
    char** picoBlock
);

/*
 * Destroys a PICO manager and frees its RWX memory block.
 * Does NOT free the vault buffers (PICO data) - caller is responsible.
 * Does NOT free data sections (they're freed individually during removal).
 *
 * @param manager    - Pointer to the PICO_MANAGER to destroy
 * @param picoBlock  - Address of the RWX memory block to free
 * @return TRUE on success, FALSE on invalid arguments
 *
 * Note: After destruction, vault pointers in entries are still valid.
 * This allows reusing vaults in a new manager created with DuplicateManager().
 */
BOOL DestroyManager(
    PPICO_MANAGER manager,
    char* picoBlock
);

// linker intrinsic to map a function hash to a hook registered via Crystal Palace
FARPROC __resolve_hook(DWORD funcHash);

/*
 * PICO running functions
 */
typedef void (*PICOMAIN_FUNC)(char * arg);

PICOMAIN_FUNC PicoGetExport(char * src, char * base, int tag);
PICOMAIN_FUNC PicoEntryPoint(char * src, char * base);
int PicoCodeSize(char * src);
int PicoDataSize(char * src);
void PicoLoad(IMPORTFUNCS * funcs, char * src, char * dstCode, char * dstData);

/*
 * A macro to figure out our caller
 * https://github.com/rapid7/ReflectiveDLLInjection/blob/81cde88bebaa9fe782391712518903b5923470fb/dll/src/ReflectiveLoader.c#L34C1-L46C1
 */
#ifdef __MINGW32__
#define WIN_GET_CALLER() __builtin_extract_return_addr(__builtin_return_address(0))
#else
#pragma intrinsic(_ReturnAddress)
#define WIN_GET_CALLER() _ReturnAddress()
#endif

#endif /* PICO_MANAGER_H */