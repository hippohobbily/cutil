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
#define open _open
#define write _write
#define close _close
#define O_BINARY _O_BINARY
#define O_CREAT _O_CREAT
#define O_WRONLY _O_WRONLY
#define O_TRUNC _O_TRUNC
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
    ssize_t written;
    size_t i;
    char formatted_size[64];
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Allocating %s of memory...\n", formatted_size);
    
    buffer = (unsigned char *)malloc(size);
    if (buffer == NULL) {
        fprintf(stderr, "Error: Cannot allocate %llu bytes of memory: %s\n", 
                size, strerror(errno));
        return 1;
    }
    
    printf("Initializing memory with pattern...\n");
    for (i = 0; i < size; i++) {
        buffer[i] = (unsigned char)(i & 0xFF);
    }
    
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        free(buffer);
        return 1;
    }
    
    printf("Writing %s to file '%s' (single write)...\n", formatted_size, filename);
    
    written = write(fd, buffer, size);
    if (written < 0) {
        fprintf(stderr, "Error: Write failed: %s\n", strerror(errno));
        close(fd);
        free(buffer);
        return 1;
    }
    
    if ((size_t)written != size) {
        fprintf(stderr, "Error: Partial write - wrote %zd bytes of %llu\n", 
                written, size);
        close(fd);
        free(buffer);
        return 1;
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
    int i;
    char formatted_size[64];
    int percent, last_percent = -1;
    
    /* Initialize buffer with pattern */
    for (i = 0; i < BUFFER_SIZE; i++) {
        buffer[i] = (unsigned char)(i & 0xFF);
    }
    
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Writing %s to file '%s' (pwrite mode)...\n", formatted_size, filename);
    
    while (written < size) {
        size_t to_write = (size - written > BUFFER_SIZE) ? BUFFER_SIZE : (size_t)(size - written);
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
int write_file_writev(const char *filename, unsigned long long size) {
    int fd;
    struct iovec *iov;
    int iovcnt;
    size_t chunk_size = 1024 * 1024; /* 1MB chunks */
    size_t remaining;
    ssize_t written;
    int i;
    char formatted_size[64];
    unsigned char *buffers;
    
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    
    /* Calculate number of iovec structures needed */
    iovcnt = (size + chunk_size - 1) / chunk_size;
    if (iovcnt > 1024) iovcnt = 1024; /* Limit to reasonable number */
    
    iov = (struct iovec *)malloc(iovcnt * sizeof(struct iovec));
    if (iov == NULL) {
        fprintf(stderr, "Error: Cannot allocate iovec array\n");
        close(fd);
        return 1;
    }
    
    /* Allocate all buffers */
    buffers = (unsigned char *)malloc(size);
    if (buffers == NULL) {
        fprintf(stderr, "Error: Cannot allocate %llu bytes\n", size);
        free(iov);
        close(fd);
        return 1;
    }
    
    /* Initialize buffer with pattern */
    for (i = 0; i < size; i++) {
        buffers[i] = (unsigned char)(i & 0xFF);
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Writing %s to file '%s' (writev mode with %d vectors)...\n", 
           formatted_size, filename, iovcnt);
    
    /* Setup iovec array */
    remaining = size;
    for (i = 0; i < iovcnt && remaining > 0; i++) {
        size_t this_chunk = (remaining > chunk_size) ? chunk_size : remaining;
        iov[i].iov_base = buffers + (i * chunk_size);
        iov[i].iov_len = this_chunk;
        remaining -= this_chunk;
    }
    
    written = writev(fd, iov, i);
    if (written < 0) {
        fprintf(stderr, "Error: writev failed: %s\n", strerror(errno));
        free(buffers);
        free(iov);
        close(fd);
        return 1;
    }
    
    if ((size_t)written != size) {
        fprintf(stderr, "Error: Partial write - wrote %zd bytes of %llu\n", 
                written, size);
        free(buffers);
        free(iov);
        close(fd);
        return 1;
    }
    
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
    int iovcnt;
    size_t chunk_size = 1024 * 1024; /* 1MB chunks */
    size_t remaining;
    ssize_t written;
    int i;
    char formatted_size[64];
    unsigned char *buffers;
    
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    
    /* Calculate number of iovec structures needed */
    iovcnt = (size + chunk_size - 1) / chunk_size;
    if (iovcnt > 1024) iovcnt = 1024; /* Limit to reasonable number */
    
    iov = (struct iovec *)malloc(iovcnt * sizeof(struct iovec));
    if (iov == NULL) {
        fprintf(stderr, "Error: Cannot allocate iovec array\n");
        close(fd);
        return 1;
    }
    
    /* Allocate all buffers */
    buffers = (unsigned char *)malloc(size);
    if (buffers == NULL) {
        fprintf(stderr, "Error: Cannot allocate %llu bytes\n", size);
        free(iov);
        close(fd);
        return 1;
    }
    
    /* Initialize buffer with pattern */
    for (i = 0; i < size; i++) {
        buffers[i] = (unsigned char)(i & 0xFF);
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Writing %s to file '%s' (pwritev mode with %d vectors)...\n", 
           formatted_size, filename, iovcnt);
    
    /* Setup iovec array */
    remaining = size;
    for (i = 0; i < iovcnt && remaining > 0; i++) {
        size_t this_chunk = (remaining > chunk_size) ? chunk_size : remaining;
        iov[i].iov_base = buffers + (i * chunk_size);
        iov[i].iov_len = this_chunk;
        remaining -= this_chunk;
    }
    
    written = pwritev(fd, iov, i, 0); /* Write at offset 0 */
    if (written < 0) {
        fprintf(stderr, "Error: pwritev failed: %s\n", strerror(errno));
        free(buffers);
        free(iov);
        close(fd);
        return 1;
    }
    
    if ((size_t)written != size) {
        fprintf(stderr, "Error: Partial write - wrote %zd bytes of %llu\n", 
                written, size);
        free(buffers);
        free(iov);
        close(fd);
        return 1;
    }
    
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
    int i;
    char formatted_size[64];
    int percent, last_percent = -1;
    
    /* Initialize buffer with pattern */
    for (i = 0; i < BUFFER_SIZE; i++) {
        buffer[i] = (unsigned char)(i & 0xFF);
    }
    
    fp = fopen(filename, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Error: Cannot open file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    
    format_size(size, formatted_size, sizeof(formatted_size));
    printf("Writing %s to file '%s' (stream mode)...\n", formatted_size, filename);
    
    while (written < size) {
        to_write = (size - written > BUFFER_SIZE) ? BUFFER_SIZE : (size_t)(size - written);
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

void print_usage(const char *program_name) {
    printf("Usage: %s [-m|-p|-v|-pv] <size> <filename>\n", program_name);
    printf("\nWrite modes:\n");
    printf("  (default) Stream mode using fwrite() with progress indicator\n");
    printf("  -m        Malloc mode (allocate entire file in memory, single write())\n");
#if HAVE_PWRITE
    printf("  -p        Positioned write mode using pwrite() syscall\n");
#endif
#if HAVE_WRITEV
    printf("  -v        Vectored I/O mode using writev() syscall\n");
#endif
#if HAVE_PWRITEV
    printf("  -pv       Positioned vectored I/O using pwritev() syscall\n");
#endif
    printf("\nSize formats:\n");
    printf("  Decimal bytes:  1024\n");
    printf("  Hex bytes:      0x400\n");
    printf("  Human format:   2.5GB, 2.5G, 200MB, 200M, 10KB, 10K\n");
    printf("  Supported suffixes: B, K/KB, M/MB, G/GB, T/TB (case insensitive)\n");
    printf("\nExamples:\n");
    printf("  %s 2.5GB output.dat           # Stream mode (default)\n", program_name);
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
    enum { MODE_STREAM, MODE_MALLOC, MODE_PWRITE, MODE_WRITEV, MODE_PWRITEV } mode = MODE_STREAM;
    int arg_offset = 1;
    char *size_str;
    char *filename;
    
    if (argc < 3 || argc > 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    /* Check for mode flags */
    if (argc == 4) {
        if (strcmp(argv[1], "-m") == 0) {
            mode = MODE_MALLOC;
            arg_offset = 2;
        }
#if HAVE_PWRITE
        else if (strcmp(argv[1], "-p") == 0) {
            mode = MODE_PWRITE;
            arg_offset = 2;
        }
#endif
#if HAVE_WRITEV
        else if (strcmp(argv[1], "-v") == 0) {
            mode = MODE_WRITEV;
            arg_offset = 2;
        }
#endif
#if HAVE_PWRITEV
        else if (strcmp(argv[1], "-pv") == 0) {
            mode = MODE_PWRITEV;
            arg_offset = 2;
        }
#endif
        else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[1]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    size_str = argv[arg_offset];
    filename = argv[arg_offset + 1];
    
    size = parse_size(size_str);
    if (size == 0) {
        fprintf(stderr, "Error: Invalid size specification '%s'\n", size_str);
        print_usage(argv[0]);
        return 1;
    }
    
    switch (mode) {
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