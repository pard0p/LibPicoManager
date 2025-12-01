# LibPicoManager

LibPicoManager is a **unified PICO management framework** designed as a Crystal Palace shared library for C2 implant development. Its primary purpose is to provide centralized control over Position Independent Code Objects (PICOs) in memory, enabling code loading, substitution and lifecycle management.

The library provides a structured abstraction over memory allocation and PICO orchestration, hiding the complexity of memory management, entry point tracking, and dynamic module replacement to enable streamlined PICO operations with a clean, composable interface.

## Key Features

- **Unified Code Block**: Single shared RWX memory block containing all PICO code sections, reducing fragmentation and enabling coherent memory strategy for advanced techniques like sleep masking.
- **Dynamic PICO Substitution**: Replace PICO modules at runtime (e.g., swap communication transport) without affecting the overall manager state or other loaded modules.
- **Flexible Lookup**: Retrieve PICO entries by numeric ID or by name, with support for export resolution by both identifiers.

## Use Cases

### 1. Sleep Masking
The unified code block enables advanced sleep masking techniques. By having all PICO code in a single contiguous block, implants can:
- Encrypt/decrypt the entire block as a single unit
- Reduce syscall overhead during sleep operations
- Maintain consistent memory offsets and entry points across wake cycles

### 2. Communication PICO Substitution
Replace the communication PICO at runtime without generating a new implant:
```c
RemovePicoById(manager, TRANSPORT_ID);      // Remove old transport
AddPico(manager, "transport", newVault);    // Add new transport
LoadPico(manager, manager->entryCount - 1, padding, funcs);  // Load only new transport
```

This pattern enables:
- Dynamic fallback to alternative C2 channels.
- Protocol switching based on network conditions.
- Seamless migration between communication methods.

## API Reference

### Core Types

#### `PICO_ENTRY`
Individual PICO module entry containing metadata and execution context.
- `id`: Unique numeric identifier (0-based, updated on removal).
- `name`: Module name for lookup (max 32 characters, null-terminated).
- `code`: Pointer to code section in shared RWX block (NULL if not loaded).
- `codeSize`: Size of code section in bytes.
- `data`: Pointer to separate RW data block (NULL if not loaded).
- `dataSize`: Size of data section in bytes.
- `entryPoint`: Module entry point function (NULL if not loaded).
- `vault`: Pointer to original PICO buffer (read-only reference, always valid).

#### `PICO_MANAGER`
Central manager structure coordinating all PICO modules and shared memory.
- `baseAddress`: Base address of shared RWX memory block.
- `blockSize`: Total allocated size of RWX block in bytes.
- `usedSize`: Currently used portion of RWX block in bytes.
- `entries`: Pointer to array of PICO_ENTRY structures.
- `entryCount`: Number of currently registered PICOs (updated on add/remove).
- `entryCapacity`: Maximum capacity of entries array.
- `interPicoPadding`: Padding between PICOs in shared block (bytes).

#### `IMPORTFUNCS`
Import function table passed to PICO loaders.
- `LoadLibraryA`: Function pointer to LoadLibraryA.
- `GetProcAddress`: Function pointer to GetProcAddress.

### Core Functions

#### `PicoManagerInit`
Initializes the PICO manager structure. Does NOT allocate the RWX block.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure to initialize.
  - `entries`: Pointer to array of PICO_ENTRY structures.
  - `entryCapacity`: Maximum number of entries the array can hold.
- **Returns**: void (does not fail).
- **Notes**: Caller is responsible for allocating the RWX block later via `PicoManagerAlloc()`.

#### `AddPico`
Registers a new PICO module in the manager. Only stores metadata and vault reference.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure.
  - `name`: Module name (null-terminated string, max 31 characters).
  - `vault`: Pointer to original PICO buffer (must remain valid).
- **Returns**: TRUE on success, FALSE if manager is full or arguments are invalid.
- **Notes**: Does not allocate memory or load code. Code sizes are extracted from vault via `PicoCodeSize()` and `PicoDataSize()`.

#### `PicoManagerAlloc`
Allocates the shared RWX memory block for storing PICO code sections.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure (must have PICOs already added).
  - `finalPadding`: Additional padding in bytes to reserve at end of block.
- **Returns**: TRUE on success, FALSE if allocation failed.
- **Notes**: Call after adding all PICOs for a given phase. Can be called multiple times if needed.

#### `LoadPico`
Loads registered but not yet loaded PICOs into the manager's RWX block.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure (must have allocated block).
  - `upToEntryId`: Load entries up to and including this ID (0-based), or -1 for all.
  - `finalPadding`: Additional padding to reserve at end.
  - `funcs`: IMPORTFUNCS structure for PICO loader.
- **Returns**: TRUE on success, FALSE if insufficient space or loading failed.
- **Notes**: Can be called multiple times for phased loading. Already-loaded PICOs are skipped. Example: `LoadPico(mgr, 0, 50, funcs)` loads only entry 0 (hooks).

#### `RemovePicoById`
Removes a PICO entry by numeric ID. Frees data block and compacts array.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure.
  - `id`: Numeric ID of entry to remove (0-based).
- **Returns**: TRUE on success, FALSE if ID is invalid.
- **Behavior**: 
  - Frees the PICO's data block.
  - Shifts all subsequent entries left (compacts array).
  - Recalculates IDs of shifted entries.
  - Decrements `entryCount`.
- **Example**: `[A(id=0), B(id=1), C(id=2)]` → `RemovePicoById(mgr, 1)` → `[A(id=0), C(id=1)]`

#### `RemovePicoByName`
Removes a PICO entry by name. Frees data block and compacts array.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure.
  - `name`: Name of entry to remove (null-terminated string).
- **Returns**: TRUE on success, FALSE if name is not found.
- **Behavior**: Identical to `RemovePicoById()`, but looks up by name first.

#### `GetPicoById`
Retrieves a PICO entry by numeric ID.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure.
  - `id`: Numeric ID of entry (0-based).
- **Returns**: Pointer to PICO_ENTRY, or NULL if not found.

#### `GetPicoByName`
Retrieves a PICO entry by name.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure.
  - `name`: Name of entry (null-terminated string).
- **Returns**: Pointer to PICO_ENTRY, or NULL if not found.
- **Notes**: Case-sensitive string comparison.

#### `GetPicoExportById`
Retrieves an export from a PICO module by ID and tag.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure.
  - `id`: Numeric ID of PICO entry.
  - `tag`: Export tag identifier.
- **Returns**: Export address as char*, or NULL if not found.
- **Notes**: PICO must be loaded. Delegates to `PicoGetExport()` from LibTcg.

#### `GetPicoExportByName`
Retrieves an export from a PICO module by name and tag.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure.
  - `name`: Name of PICO module (null-terminated string).
  - `tag`: Export tag identifier.
- **Returns**: Export address as char*, or NULL if not found.
- **Notes**: PICO must be loaded.

#### `TotalCodeSize`
Calculates total code size required for all registered PICOs.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER structure.
- **Returns**: Total size in bytes including inter-PICO padding.
- **Notes**: Includes padding between modules but excludes final padding. Works with unloaded entries.

#### `DuplicateManager`
Duplicates the PICO manager with a new RWX block.
- **Parameters**:
  - `manager`: Pointer to source PICO_MANAGER.
  - `newManager`: Pointer to new PICO_MANAGER to initialize.
  - `entries`: Array of PICO_ENTRY structures for new manager.
  - `entryCapacity`: Maximum capacity of new entries array.
  - `picoBlock`: Output parameter receiving address of allocated RWX block.
- **Returns**: TRUE on success, FALSE on failure.
- **Behavior**:
  - Initializes new manager.
  - Copies all vault references from source manager.
  - Allocates new RWX block.
  - Does NOT load PICOs yet.
- **Notes**: Use for dynamic reallocation when initial block is insufficient.

#### `DestroyManager`
Destroys a PICO manager and frees its RWX code block.
- **Parameters**:
  - `manager`: Pointer to PICO_MANAGER to destroy.
  - `picoBlock`: Address of RWX block to free.
- **Returns**: TRUE on success, FALSE on invalid arguments.
- **Notes**: 
  - Does NOT free vault buffers (caller responsibility).
  - Does NOT free individual data sections (freed during removal).
  - Vault pointers remain valid for reuse in new managers.

## Design Patterns

### Pattern 1: Basic Multi-Phase Loading
```c
// Phase 1: Register modules
AddPico(manager, "hooks", hooksVault);
AddPico(manager, "transport", transportVault);
AddPico(manager, "commands", commandsVault);

// Phase 2: Allocate shared block
PicoManagerAlloc(manager, 100);  // 100 bytes final padding

// Phase 3: Load hooks, execute for import resolution
LoadPico(manager, 0, 100, &importFuncs);
((HOOK_FUNC)GetPicoById(manager, 0)->entryPoint)(&importFuncs);

// Phase 4: Load all remaining modules with hooked functions
LoadPico(manager, -1, 100, &importFuncs);
```

### Pattern 2: PICO Substitution
```c
// Find current transport PICO
PPICO_ENTRY oldTransport = GetPicoByName(manager, "transport");
DWORD transportId = oldTransport->id;

// Remove old transport
RemovePicoById(manager, transportId);

// Add new transport
AddPico(manager, "transport", newTransportVault);

// Load only the new transport
LoadPico(manager, manager->entryCount - 1, 100, &importFuncs);
```

### Pattern 3: Dynamic Reallocation
```c
// Initial load fails due to insufficient space
if (!LoadPico(manager, -1, 100, &importFuncs)) {
    // Allocate larger manager
    PICO_MANAGER newManager;
    char* newBlock;
    
    DuplicateManager(manager, &newManager, entries, capacity, &newBlock);
    
    // Load hooks in new block
    LoadPico(&newManager, 0, 100, &importFuncs);
    
    // Execute hooks for import resolution
    // ... execute hooks ...
    
    // Load all remaining with updated imports
    LoadPico(&newManager, -1, 100, &importFuncs);
    
    PPICO_ENTRY implantEntry = GetPicoByName(newManager, "implant");
    
    // Our implant shoud call DestroyManager in order to free the old RWX block
    ((IMPLANT_ENTRY)implantEntry->entryPoint)(manager, newManager);
}
```
