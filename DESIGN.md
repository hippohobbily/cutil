# writefile Design Document

## Overview
`writefile` is a file creation utility designed for testing file I/O operations, filesystem performance, and storage integrity. It creates files of specific sizes filled with deterministic patterns that can be verified later.

## Key Features
- Multiple write modes (stream, malloc, pwrite, writev, pwritev)
- Deterministic 32-bit pattern generation based on file offset
- Pattern verification capability
- Chunking support for very large files
- Verbose debugging output
- Cross-platform compatibility (Linux, BSD, AIX, Windows partial)

## Architecture

### Pattern Generation
The core of writefile's integrity checking is its deterministic pattern generation:

```c
static unsigned int generate_pattern(unsigned long long offset) {
    unsigned char b0 = (offset >> 16) & 0xFF;
    unsigned char b1 = (offset >> 8) & 0xFF;
    unsigned char b2 = offset & 0xFF;
    unsigned char b3 = (b0 ^ b1 ^ b2) + 0x55;  /* Simple checksum */
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}
```

Each 4-byte sequence in the file is unique to its position, allowing detection of:
- Write ordering issues
- Data corruption or bit flips
- Partial or failed writes
- Filesystem integrity problems

### Write Modes

#### 1. Stream Mode (Default)
- Uses `fwrite()` for buffered I/O
- 8KB buffer size for efficiency
- Progress indicator during writes
- Most portable across platforms

#### 2. Malloc Mode (-m)
- Allocates entire file size in memory
- Single `write()` system call (or chunked if -C is used)
- Tests large memory allocations
- Useful for atomic write testing

#### 3. Positioned Write Mode (-p)
- Uses `pwrite()` system call
- Writes at specific offsets without changing file position
- Available on POSIX.1-2001 systems
- Good for concurrent write testing

#### 4. Vectored I/O Mode (-v)
- Uses `writev()` system call
- Writes multiple buffers in single call
- Reduces system call overhead
- Platform-specific optimizations (especially AIX)

#### 5. Positioned Vectored Mode (-pv)
- Uses `pwritev()` system call
- Combines positioned writes with vectored I/O
- Linux and BSD specific
- Most efficient for certain workloads

### Chunking Implementation (-C flag)

The chunking feature addresses several limitations:
1. **System limits on single write operations** - Some systems limit single write() calls
2. **Memory allocation constraints** - Very large mallocs may fail
3. **Debugging large writes** - Easier to trace issues with smaller chunks

**Implementation Details:**
- Maximum chunk size: `0x7fff0000` bytes (slightly under 2GB)
- Only works with malloc mode (-m)
- Automatically divides large files into manageable chunks
- Maintains pattern continuity across chunks
- Verbose output shows chunk boundaries

**Chunking Algorithm:**
```c
if (use_chunking && size > MAX_CHUNK_SIZE) {
    // Calculate number of full chunks and remainder
    num_full_chunks = size / MAX_CHUNK_SIZE;
    remainder = size % MAX_CHUNK_SIZE;
    
    // Write in chunks
    while (total_written < size) {
        to_write = min(remaining, MAX_CHUNK_SIZE);
        write(fd, buffer + total_written, to_write);
        total_written += written;
    }
}
```

### Platform Compatibility

#### AIX Specific Optimizations
- Avoids `inline` keyword (compiler compatibility)
- Conservative `IOV_MAX` limit (16 vectors)
- Reduced chunk size for `writev()` (64KB)
- Careful handling of system limits

#### Windows Support (Partial)
- Uses `_open`, `_write`, `_close` equivalents
- Binary mode handling (`O_BINARY`)
- No support for pwrite/writev variants
- Sleep() adaptation for milliseconds

#### Feature Detection
Compile-time detection using preprocessor:
- `HAVE_PWRITE` - Positioned write support
- `HAVE_WRITEV` - Vectored I/O support
- `HAVE_PWRITEV` - Positioned vectored I/O

### Debug Features

#### Wait for Cookie (-w)
Pauses execution until `/tmp/zcookie` exists:
- Allows attaching debuggers
- Enables system monitoring setup
- Useful for race condition testing

#### Verbose Mode (-V)
Detailed output includes:
- System call parameters
- File descriptors
- Buffer addresses and sizes
- Return values and errno
- Chunk boundaries (when using -C)

### Error Handling

Robust error handling throughout:
- EINTR signal handling (retry interrupted writes)
- Partial write detection and continuation
- Memory allocation failure handling
- File operation error reporting
- Pattern verification with error limits

### Verification Mode (-c)

Reads file and verifies pattern integrity:
- Stops after 10 errors (configurable)
- Reports offset of mismatches
- Shows expected vs actual values
- Progress indicator during verification

## Performance Considerations

1. **Buffer Sizes**: 8KB default, 1MB for vectored modes
2. **Chunk Sizes**: 64KB for AIX, 1MB for others
3. **Memory Usage**: Full file size for malloc mode
4. **System Calls**: Minimized through buffering/vectoring

## Testing Scenarios

Common use cases:
1. **Filesystem limits**: Test maximum file sizes
2. **Write atomicity**: Single large write vs multiple chunks
3. **Performance**: Compare different write modes
4. **Integrity**: Verify pattern after system crashes
5. **Debugging**: Trace I/O operations with -V flag

## Future Enhancements

Potential improvements:
1. Async I/O support (aio_write)
2. Direct I/O mode (O_DIRECT)
3. Multi-threaded writes
4. Network file writing
5. Compression support
6. Custom pattern generators
7. JSON output for test automation

## Building

Simple compilation:
```bash
# Basic build
gcc -o writefile writefile.c

# With optimizations
gcc -O2 -o writefile writefile.c

# For AIX
xlc -o writefile writefile.c -D_AIX

# For Windows (partial support)
cl writefile.c /Fewritefile.exe
```

## Security Considerations

- No privilege escalation
- Validates all inputs
- No shell command execution
- Careful path handling
- Pattern includes checksum for integrity