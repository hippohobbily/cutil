#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>  /* For Sleep() */
#define open _open
#define write _write
#define close _close
#define O_BINARY _O_BINARY
#define O_CREAT _O_CREAT
#define O_WRONLY _O_WRONLY
#define O_TRUNC _O_TRUNC
#define sleep(x) Sleep((x) * 1000)  /* Windows Sleep uses milliseconds */
/* Windows doesn't have pwrite/writev/pwritev */
#define HAVE_PWRITE 0
#define HAVE_WRITEV 0
#define HAVE_PWRITEV 0
#else
#include <unistd.h>
#include <sys/uio.h>
#define O_BINARY 0
/* Check for pwrite support */
#if defined(_POSIX_VERSION) && _POSIX_VERSION >= 200112L
#define HAVE_PWRITE 1
#else
#define HAVE_PWRITE 0
#endif
/* Most Unix systems have writev */
#define HAVE_WRITEV 1
/* pwritev is Linux/BSD specific */
#if defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#define HAVE_PWRITEV 1
#else
#define HAVE_PWRITEV 0
#endif
#endif

#define BUFFER_SIZE 8192

/* Generate a 32-bit pattern value based on offset.
 * This creates a unique 4-byte sequence for every position,
 * allowing verification of write order and integrity.
 * Pattern: [offset_byte3][offset_byte2][offset_byte1][checksum_byte]
 * where checksum = (byte3 ^ byte2 ^ byte1) + 0x55
 */
#ifdef _AIX
static unsigned int generate_pattern(unsigned long long offset) {
#else
static inline unsigned int generate_pattern(unsigned long long offset) {
#endif
    unsigned char b0 = (offset >> 16) & 0xFF;
    unsigned char b1 = (offset >> 8) & 0xFF;
    unsigned char b2 = offset & 0xFF;
    unsigned char b3 = (b0 ^ b1 ^ b2) + 0x55;  /* Simple checksum */
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

/* Fill buffer with 32-bit pattern starting at given file offset */
static void fill_buffer_with_pattern(unsigned char *buffer, size_t size, unsigned long long file_offset) {
    size_t i;
    for (i = 0; i + 3 < size; i += 4) {
        unsigned int pattern = generate_pattern(file_offset + i);
        buffer[i] = (pattern >> 24) & 0xFF;
        buffer[i+1] = (pattern >> 16) & 0xFF;
        buffer[i+2] = (pattern >> 8) & 0xFF;
        buffer[i+3] = pattern & 0xFF;
    }
    /* Handle remaining bytes if size is not multiple of 4 */
    if (i < size) {
        unsigned int pattern = generate_pattern(file_offset + i);
        int remaining = size - i;
        if (remaining >= 1) buffer[i] = (pattern >> 24) & 0xFF;
        if (remaining >= 2) buffer[i+1] = (pattern >> 16) & 0xFF;
        if (remaining >= 3) buffer[i+2] = (pattern >> 8) & 0xFF;
    }
}

unsigned long long parse_size(const char *str) {
    char *endptr;
    double value;
    unsigned long long result;
    
    if (str == NULL || *str == '\0') {
        fprintf(stderr, "Error: Empty size string\n");
        return 0;
    }
    
    /* Check for hex format (0x prefix) */
    if (strncmp(str, "0x", 2) == 0 || strncmp(str, "0X", 2) == 0) {
        result = strtoull(str, &endptr, 16);
        if (*endptr == '\0') {
            return result;
        }
    }
    
    /* Parse as decimal/float number */
    value = strtod(str, &endptr);
    if (value < 0) {
        fprintf(stderr, "Error: Size cannot be negative\n");
        return 0;
    }
    
    /* Check for suffix */
    if (*endptr != '\0') {
        /* Skip any whitespace */
        while (isspace(*endptr)) endptr++;
        
        /* Parse size suffix */
        if (strcasecmp(endptr, "B") == 0) {
            result = (unsigned long long)value;
        } else if (strcasecmp(endptr, "K") == 0 || strcasecmp(endptr, "KB") == 0) {
            result = (unsigned long long)(value * 1024);
        } else if (strcasecmp(endptr, "M") == 0 || strcasecmp(endptr, "MB") == 0) {
            result = (unsigned long long)(value * 1024 * 1024);
        } else if (strcasecmp(endptr, "G") == 0 || strcasecmp(endptr, "GB") == 0) {
            result = (unsigned long long)(value * 1024 * 1024 * 1024);
        } else if (strcasecmp(endptr, "T") == 0 || strcasecmp(endptr, "TB") == 0) {
            result = (unsigned long long)(value * 1024ULL * 1024ULL * 1024ULL * 1024ULL);
        } else {
            fprintf(stderr, "Error: Unknown size suffix '%s'\n", endptr);
            return 0;
        }
    } else {
        /* No suffix, treat as bytes */
        result = (unsigned long long)value;
    }
    
    return result;
}

void format_size(unsigned long long bytes, char *buffer, size_t bufsize) {
    if (bytes >= 1099511627776ULL) {
        snprintf(buffer, bufsize, "%.2f TB", bytes / 1099511627776.0);
    } else if (bytes >= 1073741824ULL) {
        snprintf(buffer, bufsize, "%.2f GB", bytes / 1073741824.0);
    } else if (bytes >= 1048576ULL) {
        snprintf(buffer, bufsize, "%.2f MB", bytes / 1048576.0);
    } else if (bytes >= 1024ULL) {
        snprintf(buffer, bufsize, "%.2f KB", bytes / 1024.0);
    } else {
        snprintf(buffer, bufsize, "%llu bytes", bytes);
    }
}

int write_file_malloc(const char *filename, unsigned long long size) {
    int fd;
    unsigned char *buffer;
    char formatted_size[64];
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Allocating %s (0x%llX bytes) of memory...\n", formatted_size, size);
    
    buffer = (unsigned char *)malloc(size);
    if (buffer == NULL) {
        fprintf(stderr, "Error: Cannot allocate %llu bytes of memory: %s\n", 
                size, strerror(errno));
        return 1;
    }
    
    printf("Initializing memory with 32-bit pattern...\n");
    fill_buffer_with_pattern(buffer, size, 0);
    
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        free(buffer);
        return 1;
    }
    
    printf("Writing %s to file '%s' (malloc mode)...\n", formatted_size, filename);
    
    size_t total_written = 0;
    while (total_written < size) {
        ssize_t written = write(fd, buffer + total_written, size - total_written);
        if (written < 0) {
            if (errno == EINTR) {
                /* Interrupted by signal, retry */
                continue;
            }
            fprintf(stderr, "Error: Write failed at %zu bytes: %s\n", 
                    total_written, strerror(errno));
            close(fd);
            free(buffer);
            return 1;
        }
        
        if (written == 0) {
            /* Shouldn't happen with regular files, but check anyway */
            fprintf(stderr, "Error: Write returned 0 at %zu bytes\n", total_written);
            close(fd);
            free(buffer);
            return 1;
        }
        
        total_written += written;
        
        /* Show progress if partial write occurred */
        if (total_written < size) {
            printf("Partial write: wrote %zu of %llu bytes, continuing...\n", 
                   total_written, size);
        }
    }
    
    close(fd);
    free(buffer);
    
    printf("Successfully wrote %s to '%s' (malloc mode)\n", formatted_size, filename);
    
    return 0;
}

#if HAVE_PWRITE
int write_file_pwrite(const char *filename, unsigned long long size) {
    int fd;
    unsigned char buffer[BUFFER_SIZE];
    unsigned long long written = 0;
    ssize_t write_result;
    char formatted_size[64];
    int percent, last_percent = -1;
    
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Writing %s to file '%s' (pwrite mode with 32-bit pattern)...\n", formatted_size, filename);
    
    while (written < size) {
        size_t to_write = (size - written > BUFFER_SIZE) ? BUFFER_SIZE : (size_t)(size - written);
        
        /* Fill buffer with pattern for current offset */
        fill_buffer_with_pattern(buffer, to_write, written);
        write_result = pwrite(fd, buffer, to_write, written);
        
        if (write_result < 0) {
            fprintf(stderr, "\nError: pwrite failed at %llu bytes: %s\n", 
                    written, strerror(errno));
            close(fd);
            return 1;
        }
        
        written += write_result;
        
        /* Show progress */
        percent = (int)((written * 100) / size);
        if (percent != last_percent) {
            printf("\rProgress: %d%%", percent);
            fflush(stdout);
            last_percent = percent;
        }
    }
    
    printf("\rProgress: 100%%\n");
    close(fd);
    
    printf("Successfully wrote %s to '%s' (pwrite mode)\n", formatted_size, filename);
    return 0;
}
#endif

#if HAVE_WRITEV
#include <limits.h>
#ifndef IOV_MAX
#ifdef _SC_IOV_MAX
#define IOV_MAX sysconf(_SC_IOV_MAX)
#else
#define IOV_MAX 16  /* Conservative default for AIX */
#endif
#endif

int write_file_writev(const char *filename, unsigned long long size) {
    int fd;
    struct iovec *iov;
    int iovcnt, max_iov;
    size_t chunk_size = 1024 * 1024; /* 1MB chunks */
    unsigned long long total_written = 0;
    ssize_t written;
    int i;
    char formatted_size[64];
    unsigned char *buffers;
    int percent, last_percent = -1;
    
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    
    /* Get system's IOV_MAX limit */
#ifdef _SC_IOV_MAX
    max_iov = sysconf(_SC_IOV_MAX);
    if (max_iov < 0) max_iov = 16; /* Fallback for AIX */
#else
    max_iov = IOV_MAX;
#endif
    
    /* For AIX, use smaller chunks and fewer vectors */
#ifdef _AIX
    max_iov = (max_iov > 16) ? 16 : max_iov;  /* AIX typically works better with 16 or fewer */
    chunk_size = 64 * 1024; /* 64KB chunks for AIX */
#endif
    
    /* Calculate number of iovec structures needed per call */
    iovcnt = (size + chunk_size - 1) / chunk_size;
    if (iovcnt > max_iov) iovcnt = max_iov;
    
    iov = (struct iovec *)malloc(iovcnt * sizeof(struct iovec));
    if (iov == NULL) {
        fprintf(stderr, "Error: Cannot allocate iovec array\n");
        close(fd);
        return 1;
    }
    
    /* Allocate buffer for one batch of vectors */
    size_t batch_size = iovcnt * chunk_size;
    if (batch_size > size) batch_size = size;
    
    buffers = (unsigned char *)malloc(batch_size);
    if (buffers == NULL) {
        fprintf(stderr, "Error: Cannot allocate %zu bytes\n", batch_size);
        free(iov);
        close(fd);
        return 1;
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Writing %s to file '%s' (writev mode with 32-bit pattern, max %d vectors per call)...\n", 
           formatted_size, filename, iovcnt);
    
    /* Write in batches */
    while (total_written < size) {
        size_t remaining_in_batch = size - total_written;
        if (remaining_in_batch > batch_size) remaining_in_batch = batch_size;
        
        /* Fill buffer with pattern for current offset */
        fill_buffer_with_pattern(buffers, remaining_in_batch, total_written);
        
        /* Setup iovec array for this batch */
        int vecs_this_batch = 0;
        size_t bytes_in_batch = 0;
        for (i = 0; i < iovcnt && bytes_in_batch < remaining_in_batch; i++) {
            size_t this_chunk = (remaining_in_batch - bytes_in_batch > chunk_size) 
                               ? chunk_size : (remaining_in_batch - bytes_in_batch);
            iov[i].iov_base = buffers + bytes_in_batch;
            iov[i].iov_len = this_chunk;
            bytes_in_batch += this_chunk;
            vecs_this_batch++;
        }
        
        written = writev(fd, iov, vecs_this_batch);
        if (written < 0) {
            fprintf(stderr, "Error: writev failed: %s (errno=%d, vecs=%d)\n", 
                    strerror(errno), errno, vecs_this_batch);
            free(buffers);
            free(iov);
            close(fd);
            return 1;
        }
        
        if ((size_t)written != bytes_in_batch) {
            fprintf(stderr, "Error: Partial write - wrote %zd bytes of %zu\n", 
                    written, bytes_in_batch);
            free(buffers);
            free(iov);
            close(fd);
            return 1;
        }
        
        total_written += written;
        
        /* Show progress */
        percent = (int)((total_written * 100) / size);
        if (percent != last_percent) {
            printf("\rProgress: %d%%", percent);
            fflush(stdout);
            last_percent = percent;
        }
    }
    
    printf("\rProgress: 100%%\n");
    free(buffers);
    free(iov);
    close(fd);
    
    printf("Successfully wrote %s to '%s' (writev mode)\n", formatted_size, filename);
    return 0;
}
#endif

#if HAVE_PWRITEV
int write_file_pwritev(const char *filename, unsigned long long size) {
    int fd;
    struct iovec *iov;
    int iovcnt, max_iov;
    size_t chunk_size = 1024 * 1024; /* 1MB chunks */
    unsigned long long total_written = 0;
    ssize_t written;
    int i;
    char formatted_size[64];
    unsigned char *buffers;
    int percent, last_percent = -1;
    
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    
    /* Get system's IOV_MAX limit */
#ifdef _SC_IOV_MAX
    max_iov = sysconf(_SC_IOV_MAX);
    if (max_iov < 0) max_iov = 1024;
#else
    max_iov = 1024;
#endif
    
    /* Calculate number of iovec structures needed per call */
    iovcnt = (size + chunk_size - 1) / chunk_size;
    if (iovcnt > max_iov) iovcnt = max_iov;
    
    iov = (struct iovec *)malloc(iovcnt * sizeof(struct iovec));
    if (iov == NULL) {
        fprintf(stderr, "Error: Cannot allocate iovec array\n");
        close(fd);
        return 1;
    }
    
    /* Allocate buffer for one batch of vectors */
    size_t batch_size = iovcnt * chunk_size;
    if (batch_size > size) batch_size = size;
    
    buffers = (unsigned char *)malloc(batch_size);
    if (buffers == NULL) {
        fprintf(stderr, "Error: Cannot allocate %zu bytes\n", batch_size);
        free(iov);
        close(fd);
        return 1;
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Writing %s to file '%s' (pwritev mode with 32-bit pattern, max %d vectors per call)...\n", 
           formatted_size, filename, iovcnt);
    
    /* Write in batches */
    while (total_written < size) {
        size_t remaining_in_batch = size - total_written;
        if (remaining_in_batch > batch_size) remaining_in_batch = batch_size;
        
        /* Fill buffer with pattern for current offset */
        fill_buffer_with_pattern(buffers, remaining_in_batch, total_written);
        
        /* Setup iovec array for this batch */
        int vecs_this_batch = 0;
        size_t bytes_in_batch = 0;
        for (i = 0; i < iovcnt && bytes_in_batch < remaining_in_batch; i++) {
            size_t this_chunk = (remaining_in_batch - bytes_in_batch > chunk_size) 
                               ? chunk_size : (remaining_in_batch - bytes_in_batch);
            iov[i].iov_base = buffers + bytes_in_batch;
            iov[i].iov_len = this_chunk;
            bytes_in_batch += this_chunk;
            vecs_this_batch++;
        }
        
        written = pwritev(fd, iov, vecs_this_batch, total_written);
        if (written < 0) {
            fprintf(stderr, "Error: pwritev failed: %s (errno=%d, vecs=%d)\n", 
                    strerror(errno), errno, vecs_this_batch);
            free(buffers);
            free(iov);
            close(fd);
            return 1;
        }
        
        if ((size_t)written != bytes_in_batch) {
            fprintf(stderr, "Error: Partial write - wrote %zd bytes of %zu\n", 
                    written, bytes_in_batch);
            free(buffers);
            free(iov);
            close(fd);
            return 1;
        }
        
        total_written += written;
        
        /* Show progress */
        percent = (int)((total_written * 100) / size);
        if (percent != last_percent) {
            printf("\rProgress: %d%%", percent);
            fflush(stdout);
            last_percent = percent;
        }
    }
    
    printf("\rProgress: 100%%\n");
    free(buffers);
    free(iov);
    close(fd);
    
    printf("Successfully wrote %s to '%s' (pwritev mode)\n", formatted_size, filename);
    return 0;
}
#endif

int write_file_stream(const char *filename, unsigned long long size) {
    FILE *fp;
    unsigned char buffer[BUFFER_SIZE];
    unsigned long long written = 0;
    size_t to_write;
    size_t write_result;
    char formatted_size[64];
    int percent, last_percent = -1;
    
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Writing %s to file '%s' (stream mode with 32-bit pattern)...\n", formatted_size, filename);
    
    while (written < size) {
        to_write = (size - written > BUFFER_SIZE) ? BUFFER_SIZE : (size_t)(size - written);
        
        /* Fill buffer with pattern for current offset */
        fill_buffer_with_pattern(buffer, to_write, written);
        write_result = fwrite(buffer, 1, to_write, fp);
        
        if (write_result != to_write) {
            fprintf(stderr, "\nError: Write failed at %llu bytes: %s\n", 
                    written, strerror(errno));
            fclose(fp);
            return 1;
        }
        
        written += write_result;
        
        /* Show progress */
        percent = (int)((written * 100) / size);
        if (percent != last_percent) {
            printf("\rProgress: %d%%", percent);
            fflush(stdout);
            last_percent = percent;
        }
    }
    
    printf("\rProgress: 100%%\n");
    fclose(fp);
    
    format_size(written, formatted_size, sizeof(formatted_size));
    printf("Successfully wrote %s to '%s' (stream mode)\n", formatted_size, filename);
    
    return 0;
}

/* Verify pattern at specific file offset */
int verify_file_pattern(const char *filename, unsigned long long size) {
    FILE *fp;
    unsigned char buffer[BUFFER_SIZE];
    unsigned long long verified = 0;
    size_t to_read, read_result;
    char formatted_size[64];
    int errors = 0;
    const int max_errors = 10;
    
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open file '%s' for verification: %s\n", 
                filename, strerror(errno));
        return -1;
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Verifying %s in file '%s'...\n", formatted_size, filename);
    
    while (verified < size && errors < max_errors) {
        to_read = (size - verified > BUFFER_SIZE) ? BUFFER_SIZE : (size_t)(size - verified);
        read_result = fread(buffer, 1, to_read, fp);
        
        if (read_result != to_read) {
            if (feof(fp)) {
                fprintf(stderr, "\nError: Unexpected EOF at %llu bytes (expected %llu)\n", 
                        verified + read_result, size);
            } else {
                fprintf(stderr, "\nError: Read failed at %llu bytes: %s\n", 
                        verified, strerror(errno));
            }
            fclose(fp);
            return -1;
        }
        
        /* Verify pattern */
        size_t i;
        for (i = 0; i < read_result && errors < max_errors; i++) {
            unsigned long long offset = verified + i;
            unsigned int expected = generate_pattern(offset);
            unsigned char expected_byte = (expected >> (24 - (8 * (offset & 3)))) & 0xFF;
            
            if (buffer[i] != expected_byte) {
                errors++;
                fprintf(stderr, "\nPattern mismatch at offset 0x%llX (%llu): "
                        "expected 0x%02X, got 0x%02X\n",
                        offset, offset, expected_byte, buffer[i]);
            }
        }
        
        verified += read_result;
        
        /* Show progress */
        if ((verified & 0xFFFFF) == 0 || verified == size) {
            printf("\rVerified: %.1f%%", (verified * 100.0) / size);
            fflush(stdout);
        }
    }
    
    printf("\n");
    fclose(fp);
    
    if (errors > 0) {
        fprintf(stderr, "Verification FAILED: %d error%s found%s\n", 
                errors, errors == 1 ? "" : "s",
                errors >= max_errors ? " (stopped after limit)" : "");
        return errors;
    }
    
    printf("Verification PASSED: All %s verified successfully\n", formatted_size);
    return 0;
}

void print_usage(const char *program_name) {
    printf("Usage: %s [-m|-p|-v|-pv|--pwritev|-c|--verify|-w] <size> <filename>\n", program_name);
    printf("\nWrite modes:\n");
    printf("  (default)  Stream mode using fwrite() with progress indicator\n");
    printf("  -m         Malloc mode (allocate entire file in memory, single write())\n");
#if HAVE_PWRITE
    printf("  -p         Positioned write mode using pwrite() syscall\n");
#endif
#if HAVE_WRITEV
    printf("  -v         Vectored I/O mode using writev() syscall\n");
#endif
#if HAVE_PWRITEV
    printf("  -pv        Positioned vectored I/O using pwritev() syscall\n");
    printf("  --pwritev  Same as -pv\n");
#endif
    printf("\nVerification mode:\n");
    printf("  -c         Verify file contents match expected pattern\n");
    printf("  --verify   Same as -c\n");
    printf("\nDebug options:\n");
    printf("  -w         Wait for /tmp/zcookie file before proceeding (for debug setup)\n");
    printf("\nSize formats:\n");
    printf("  Decimal bytes:  1024\n");
    printf("  Hex bytes:      0x400\n");
    printf("  Human format:   2.5GB, 2.5G, 200MB, 200M, 10KB, 10K\n");
    printf("  Supported suffixes: B, K/KB, M/MB, G/GB, T/TB (case insensitive)\n");
    printf("\nPattern details:\n");
    printf("  Files are filled with a 32-bit pattern based on offset\n");
    printf("  Each 4-byte sequence is unique, allowing write order verification\n");
    printf("  Pattern format: [offset_byte2][offset_byte1][offset_byte0][checksum]\n");
    printf("\nExamples:\n");
    printf("  %s 2.5GB output.dat           # Create 2.5GB file\n", program_name);
    printf("  %s -c 2.5GB output.dat        # Verify 2.5GB file\n", program_name);
    printf("  %s -m 100M bigmem.dat          # Malloc mode\n", program_name);
#if HAVE_PWRITE
    printf("  %s -p 1GB positioned.dat      # Positioned write mode\n", program_name);
#endif
#if HAVE_WRITEV
    printf("  %s -v 500M vector.dat         # Vectored I/O mode\n", program_name);
#endif
#if HAVE_PWRITEV
    printf("  %s -pv 2GB pvector.dat        # Positioned vectored mode\n", program_name);
#endif
}

int main(int argc, char *argv[]) {
    unsigned long long size;
    enum { MODE_STREAM, MODE_MALLOC, MODE_PWRITE, MODE_WRITEV, MODE_PWRITEV, MODE_VERIFY } mode = MODE_STREAM;
    int arg_offset = 1;
    char *size_str;
    char *filename;
    int wait_for_cookie = 0;
    
    if (argc < 3 || argc > 5) {
        print_usage(argv[0]);
        return 1;
    }
    
    /* Check for mode flags and options */
    while (arg_offset < argc - 2) {
        if (strcmp(argv[arg_offset], "-w") == 0) {
            wait_for_cookie = 1;
            arg_offset++;
        }
        else if (strcmp(argv[arg_offset], "-m") == 0) {
            mode = MODE_MALLOC;
            arg_offset++;
        }
        else if (strcmp(argv[arg_offset], "-c") == 0 || strcmp(argv[arg_offset], "--verify") == 0) {
            mode = MODE_VERIFY;
            arg_offset++;
        }
#if HAVE_PWRITE
        else if (strcmp(argv[arg_offset], "-p") == 0) {
            mode = MODE_PWRITE;
            arg_offset++;
        }
#endif
#if HAVE_WRITEV
        else if (strcmp(argv[arg_offset], "-v") == 0) {
            mode = MODE_WRITEV;
            arg_offset++;
        }
#endif
#if HAVE_PWRITEV
        else if (strcmp(argv[arg_offset], "-pv") == 0 || strcmp(argv[arg_offset], "--pwritev") == 0) {
            mode = MODE_PWRITEV;
            arg_offset++;
        }
#endif
        else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[arg_offset]);
#if !HAVE_PWRITEV
            if (strcmp(argv[arg_offset], "-pv") == 0 || strcmp(argv[arg_offset], "--pwritev") == 0) {
                fprintf(stderr, "Note: -pv/--pwritev is not available on this platform\n");
            }
#endif
            print_usage(argv[0]);
            return 1;
        }
    }
    
    size_str = argv[arg_offset];
    filename = argv[arg_offset + 1];
    
    /* Wait for cookie file if requested */
    if (wait_for_cookie) {
        struct stat st;
        printf("Waiting for /tmp/zcookie file to proceed (for debug setup)...\n");
        printf("Create the file with: touch /tmp/zcookie\n");
        fflush(stdout);
        
        while (stat("/tmp/zcookie", &st) != 0) {
            /* File doesn't exist yet, wait 1 second and check again */
            sleep(1);
        }
        
        printf("Cookie file detected, proceeding...\n");
        /* Optionally remove the cookie file */
        unlink("/tmp/zcookie");
    }
    
    size = parse_size(size_str);
    if (size == 0) {
        fprintf(stderr, "Error: Invalid size specification '%s'\n", size_str);
        print_usage(argv[0]);
        return 1;
    }
    
    switch (mode) {
        case MODE_VERIFY:
            return verify_file_pattern(filename, size);
        case MODE_MALLOC:
            return write_file_malloc(filename, size);
#if HAVE_PWRITE
        case MODE_PWRITE:
            return write_file_pwrite(filename, size);
#endif
#if HAVE_WRITEV
        case MODE_WRITEV:
            return write_file_writev(filename, size);
#endif
#if HAVE_PWRITEV
        case MODE_PWRITEV:
            return write_file_pwritev(filename, size);
#endif
        case MODE_STREAM:
        default:
            return write_file_stream(filename, size);
    }
}