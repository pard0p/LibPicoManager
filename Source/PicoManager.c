/*
 * PICO Manager Library - Implementation
 * 
 * Provides functionality for managing Position Independent Code Objects (PICOs)
 * in memory, including registration, allocation, lookup, and removal.
 */

#include <windows.h>
#include "../Include/PicoManager.h"

/* ========================================================================
 * EXTERNAL FUNCTION DECLARATIONS
 * ======================================================================== */

DECLSPEC_IMPORT void* __cdecl MSVCRT$memset(void* dest, int c, size_t count);
DECLSPEC_IMPORT size_t __cdecl MSVCRT$strlen(const char* str);
DECLSPEC_IMPORT int __cdecl MSVCRT$strncmp(const char* str1, const char* str2, size_t count);
DECLSPEC_IMPORT char* __cdecl MSVCRT$strncpy(char* dest, const char* src, size_t count);
WINBASEAPI LPVOID WINAPI KERNEL32$VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect);
WINBASEAPI BOOL WINAPI KERNEL32$VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType);

/* ========================================================================
 * INITIALIZATION FUNCTIONS
 * ======================================================================== */

/*
 * Initializes the PICO manager structure.
 * Sets up the manager for tracking and managing PICO modules.
 * Does NOT allocate the RWX block - caller is responsible for that.
 */
void PicoManagerInit(PPICO_MANAGER manager, PPICO_ENTRY entries, DWORD entryCapacity) {
    if (!manager) return;
    
    manager->baseAddress = NULL;
    manager->blockSize = 0;
    manager->usedSize = 0;
    manager->entries = entries;
    manager->entryCount = 0;
    manager->entryCapacity = entryCapacity;
    manager->interPicoPadding = 0;
}

/*
 * Registers a new PICO module in the manager.
 * Only stores metadata and vault reference - does not allocate or load yet.
 */
BOOL AddPico(PPICO_MANAGER manager, const char* name, char* vault) {
    if (!manager || !vault || !name) return FALSE;
    if (manager->entryCount >= manager->entryCapacity) return FALSE;
    
    PPICO_ENTRY entry = &manager->entries[manager->entryCount];
    
    entry->id = manager->entryCount;
    MSVCRT$strncpy(entry->name, name, PICO_NAME_MAX_LENGTH - 1);
    entry->name[PICO_NAME_MAX_LENGTH - 1] = '\0';
    
    entry->code = NULL;
    entry->codeSize = PicoCodeSize(vault);
    entry->data = NULL;
    entry->dataSize = PicoDataSize(vault);
    entry->entryPoint = NULL;
    entry->vault = vault;
    
    manager->entryCount++;
    return TRUE;
}

/* ========================================================================
 * LOOKUP FUNCTIONS
 * ======================================================================== */

/*
 * Retrieves a PICO entry by its numeric ID.
 */
PPICO_ENTRY GetPicoById(PPICO_MANAGER manager, DWORD id) {
    if (!manager || id >= manager->entryCount) return NULL;
    return &manager->entries[id];
}

/*
 * Retrieves a PICO entry by its name.
 * Performs case-sensitive string comparison.
 */
PPICO_ENTRY GetPicoByName(PPICO_MANAGER manager, const char* name) {
    if (!manager || !name) return NULL;
    
    SIZE_T nameLen = MSVCRT$strlen(name);
    if (nameLen >= PICO_NAME_MAX_LENGTH) {
        nameLen = PICO_NAME_MAX_LENGTH - 1;
    }
    
    for (DWORD i = 0; i < manager->entryCount; i++) {
        if (MSVCRT$strncmp(manager->entries[i].name, name, nameLen) == 0 && 
            manager->entries[i].name[nameLen] == '\0') {
            return &manager->entries[i];
        }
    }
    
    return NULL;
}

/* ========================================================================
 * REMOVAL FUNCTIONS
 * ======================================================================== */

/*
 * Removes a PICO entry by ID.
 * Frees allocated memory and compacts the array by removing the entry.
 * All subsequent entries shift left and their IDs are recalculated.
 * entryCount is decremented.
 */
BOOL RemovePicoById(PPICO_MANAGER manager, DWORD id) {
    if (!manager || id >= manager->entryCount) return FALSE;
    
    PPICO_ENTRY entry = &manager->entries[id];
    
    /* Free data section (each PICO has its own RW block) */
    if (entry->data) {
        KERNEL32$VirtualFree(entry->data, 0, MEM_RELEASE);
        entry->data = NULL;
    }
    
    /* Shift all subsequent entries left to compact the array */
    for (DWORD i = id; i < manager->entryCount - 1; i++) {
        manager->entries[i] = manager->entries[i + 1];
        /* Update ID to reflect new position */
        manager->entries[i].id = i;
    }
    
    /* Clear the last entry */
    MSVCRT$memset(&manager->entries[manager->entryCount - 1], 0, sizeof(PICO_ENTRY));
    
    /* Decrement count */
    manager->entryCount--;
    
    return TRUE;
}

/*
 * Removes a PICO entry by name.
 * Frees allocated memory and compacts the array by removing the entry.
 * All subsequent entries shift left and their IDs are recalculated.
 * entryCount is decremented.
 */
BOOL RemovePicoByName(PPICO_MANAGER manager, const char* name) {
    if (!manager || !name) return FALSE;
    
    PPICO_ENTRY entry = GetPicoByName(manager, name);
    if (!entry) return FALSE;
    
    /* Calculate ID based on entry pointer */
    DWORD id = (DWORD)(entry - manager->entries);
    return RemovePicoById(manager, id);
}

/* ========================================================================
 * ALLOCATION AND LOADING FUNCTIONS
 * ======================================================================== */

/*
 * Allocates the shared RWX memory block for storing PICO code sections.
 * Calculates required size based on registered PICOs and padding.
 */
BOOL PicoManagerAlloc(PPICO_MANAGER manager, SIZE_T finalPadding) {
    if (!manager) return FALSE;
    
    /* Calculate total code size required for all registered PICOs */
    SIZE_T totalCodeSize = TotalCodeSize(manager);
    
    /* Add inter-PICO padding for all entries except last */
    SIZE_T paddingSize = 0;
    if (manager->entryCount > 0) {
        paddingSize = manager->interPicoPadding * (manager->entryCount - 1);
    }
    
    /* Add final padding */
    SIZE_T requiredBlockSize = totalCodeSize + paddingSize + finalPadding;
    
    /* Allocate new RWX block */
    manager->baseAddress = (char*)KERNEL32$VirtualAlloc(NULL, requiredBlockSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!manager->baseAddress) {
        return FALSE;
    }
    
    manager->blockSize = requiredBlockSize;
    manager->usedSize = 0;
    
    return TRUE;
}

/*
 * Loads all registered but not yet loaded PICOs into the manager's RWX block.
 * Places PICO code sections sequentially in the shared RWX block with padding.
 */
BOOL LoadPico(PPICO_MANAGER manager, DWORD upToEntryId, SIZE_T finalPadding, IMPORTFUNCS * funcs) {
    if (!manager) return FALSE;
    if (!manager->baseAddress || manager->blockSize == 0) return FALSE;
    
    SIZE_T codeOffset = 0;
    DWORD loadUpTo = (upToEntryId == (DWORD)-1) ? manager->entryCount : (upToEntryId + 1);
    
    /* Process all entries up to specified ID: skip already loaded, load new ones */
    for (DWORD i = 0; i < loadUpTo && i < manager->entryCount; i++) {
        PPICO_ENTRY entry = &manager->entries[i];
        
        /* If already loaded, just advance offset */
        if (entry->code) {
            codeOffset += entry->codeSize + manager->interPicoPadding;
            continue;
        }
        
        /* If no vault, skip empty entry */
        if (!entry->vault) continue;
        
        /* Check if there's enough space */
        SIZE_T requiredSize = entry->codeSize + manager->interPicoPadding;
        if (codeOffset + requiredSize + finalPadding > manager->blockSize) {
            return FALSE;
        }
        
        /* Assign position in shared RWX block */
        entry->code = manager->baseAddress + codeOffset;
        
        /* Allocate separate RW block for data section */
        entry->data = (char*)KERNEL32$VirtualAlloc(NULL, entry->dataSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!entry->data) {
            return FALSE;
        }
        
        /* Load the PICO */
        PicoLoad(funcs, entry->vault, entry->code, entry->data);
        
        /* Calculate entry point */
        entry->entryPoint = (char*)PicoEntryPoint(entry->vault, entry->code);
        
        /* Advance offset for next PICO */
        codeOffset += entry->codeSize + manager->interPicoPadding;
    }
    
    manager->usedSize = codeOffset;
    return TRUE;
}

/* ========================================================================
 * EXPORT LOOKUP FUNCTIONS
 * ======================================================================== */

/*
 * Retrieves an export from a PICO module by ID.
 */
char* GetPicoExportById(PPICO_MANAGER manager, DWORD id, int tag) {
    if (!manager || id >= manager->entryCount) return NULL;
    
    PPICO_ENTRY entry = &manager->entries[id];
    if (!entry->vault || !entry->code) return NULL;
    
    return (char*)PicoGetExport(entry->vault, entry->code, tag);
}

/*
 * Retrieves an export from a PICO module by name.
 */
char* GetPicoExportByName(PPICO_MANAGER manager, const char* name, int tag) {
    if (!manager || !name) return NULL;
    
    PPICO_ENTRY entry = GetPicoByName(manager, name);
    if (!entry || !entry->vault || !entry->code) return NULL;
    
    return (char*)PicoGetExport(entry->vault, entry->code, tag);
}

/* ========================================================================
 * UTILITY FUNCTIONS
 * ======================================================================== */

/*
 * Calculates the total code size required for all registered PICO modules.
 * Includes inter-PICO padding.
 */
SIZE_T TotalCodeSize(PPICO_MANAGER manager) {
    if (!manager) return 0;
    
    SIZE_T totalSize = 0;
    
    for (DWORD i = 0; i < manager->entryCount; i++) {
        PPICO_ENTRY entry = &manager->entries[i];
        
        /* Skip entries without vault (empty/removed entries) */
        if (!entry->vault) continue;
        
        /* Add code size */
        totalSize += entry->codeSize;
        
        /* Add inter-PICO padding between entries */
        if (i < manager->entryCount - 1) {
            totalSize += manager->interPicoPadding;
        }
    }
    
    return totalSize;
}

/* ========================================================================
 * ADVANCED FUNCTIONS - MANAGER DUPLICATION AND LIFECYCLE
 * ======================================================================== */

/*
 * Duplicates the PICO manager by calculating total code size and allocating
 * a new block sized appropriately for all registered PICOs.
 */
BOOL DuplicateManager(
    PPICO_MANAGER manager,
    PPICO_MANAGER newManager,
    PPICO_ENTRY entries,
    DWORD entryCapacity,
    char** picoBlock
) {
    if (!manager || !newManager || !entries || !picoBlock) return FALSE;
    
    /* Initialize new manager */
    PicoManagerInit(newManager, entries, entryCapacity);
    newManager->interPicoPadding = manager->interPicoPadding;
    
    /* Copy all vault references from old manager */
    for (DWORD i = 0; i < manager->entryCount; i++) {
        if (manager->entries[i].vault) {
            /* Add entry to new manager (without loading yet) */
            if (!AddPico(newManager, manager->entries[i].name, manager->entries[i].vault)) {
                /* Rollback on failure */
                return FALSE;
            }
        }
    }
    
    /* Allocate block for new manager */
    if (!PicoManagerAlloc(newManager, manager->entryCount * manager->interPicoPadding)) {
        return FALSE;
    }
    
    *picoBlock = newManager->baseAddress;
    return TRUE;
}

/*
 * Destroys a PICO manager and frees its RWX code block.
 * Preserves vault references and does not free individual data sections.
 */
BOOL DestroyManager(
    PPICO_MANAGER manager,
    char* picoBlock
) {
    if (!manager) return FALSE;
    
    /* Free the main RWX code block */
    if (picoBlock) {
        KERNEL32$VirtualFree(picoBlock, 0, MEM_RELEASE);
    }
    
    /* Clear manager state (optional but good practice) */
    manager->baseAddress = NULL;
    manager->blockSize = 0;
    manager->usedSize = 0;
    manager->entryCount = 0;
    
    return TRUE;
}