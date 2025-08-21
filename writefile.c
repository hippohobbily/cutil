#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _AIX
#include <unistd.h>
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

int write_file(const char *filename, unsigned long long size) {
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
    printf("Writing %s to file '%s'...\n", formatted_size, filename);
    
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
    printf("Successfully wrote %s to '%s'\n", formatted_size, filename);
    
    return 0;
}

void print_usage(const char *program_name) {
    printf("Usage: %s <size> <filename>\n", program_name);
    printf("\nSize formats:\n");
    printf("  Decimal bytes:  1024\n");
    printf("  Hex bytes:      0x400\n");
    printf("  Human format:   2.5GB, 2.5G, 200MB, 200M, 10KB, 10K\n");
    printf("  Supported suffixes: B, K/KB, M/MB, G/GB, T/TB (case insensitive)\n");
    printf("\nExamples:\n");
    printf("  %s 2.5GB output.dat\n", program_name);
    printf("  %s 2147483648 bigfile.bin\n", program_name);
    printf("  %s 0x80000000 hexsize.dat\n", program_name);
    printf("  %s 500M halfgig.dat\n", program_name);
}

int main(int argc, char *argv[]) {
    unsigned long long size;
    
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    size = parse_size(argv[1]);
    if (size == 0) {
        fprintf(stderr, "Error: Invalid size specification '%s'\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }
    
    return write_file(argv[2], size);
}