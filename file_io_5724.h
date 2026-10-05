#ifndef FILE_IO_5724_H
#define FILE_IO_5724_H

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_FILE_SIZE (1024U * 1024U)
#define FILE_QUEUE_SIZE (2U * MAX_FILE_SIZE + 8192U)

/* Filenames are single safe path components, never paths or dot entries. */
static inline int file_component_valid(const char *name)
{
    size_t n = strlen(name);
    if (!n || n > 127 || !strcmp(name, ".") || !strcmp(name, ".."))
        return 0;
    for (size_t i = 0; i < n; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return 0;
    }
    return 1;
}

static inline int file_size_parse(const char *text, size_t *size)
{
    if (!*text) return -1;
    for (const char *p = text; *p; p++)
        if (*p < '0' || *p > '9') return -1;
    errno = 0;
    char *end;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || *end) return -1;
    if (value > MAX_FILE_SIZE) return 1;
    *size = (size_t)value;
    return 0;
}

/* Open directory components without following symlinks. */
static inline int file_open_dir(int parent, const char *name)
{
    if (mkdirat(parent, name, 0700) == -1 && errno != EEXIST)
        return -1;
    return openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
}

/* Write a temporary file, then publish without overwriting an existing file.
   Nothing is written to disk until a complete payload has arrived in memory. */
static inline int file_save(const char *base, const char *owner,
                            const char *sender, const char *name,
                            const unsigned char *data, size_t size)
{
    if (!file_component_valid(owner) || !file_component_valid(sender) ||
        !file_component_valid(name))
    {
        errno = EINVAL;
        return -1;
    }
    int a = file_open_dir(AT_FDCWD, base);
    if (a == -1) return -1;
    int b = file_open_dir(a, owner);
    close(a);
    if (b == -1) return -1;
    int dir = file_open_dir(b, sender);
    close(b);
    if (dir == -1) return -1;

    static unsigned long sequence;
    char temporary[80];
    int fd = -1;
    for (int tries = 0; tries < 100; tries++)
    {
        snprintf(temporary, sizeof(temporary), ".upload-%ld-%lu.tmp",
                 (long)getpid(), ++sequence);
        fd = openat(dir, temporary,
                    O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (fd != -1 || errno != EEXIST) break;
    }
    if (fd == -1) { close(dir); return -1; }

    size_t done = 0;
    int result = 0;
    while (done < size)
    {
        ssize_t count = write(fd, data + done, size - done);
        if (count == -1 && errno == EINTR) continue;
        if (count <= 0) { if (!count) errno = EIO; result = -1; break; }
        done += (size_t)count;
    }
    if (!result && fsync(fd) == -1) result = -1;
    int saved_errno = errno;
    if (close(fd) == -1 && !result) { result = -1; saved_errno = errno; }
    if (!result && linkat(dir, temporary, dir, name, 0) == -1)
    {
        result = -1;
        saved_errno = errno;
    }
    unlinkat(dir, temporary, 0);
    close(dir);
    errno = saved_errno;
    return result;
}

#endif
