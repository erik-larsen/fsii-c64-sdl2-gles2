/*
 * shelf.c - the player's disk shelf (C90 + POSIX dirent).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "shelf.h"
#include "diskio.h"

#define SHELF_MAX 64

struct disk {
    char path[1024];
    char label[64];
    int writable;
};

static struct disk shelf[SHELF_MAX];
static int count;
static int current = -1;

static int by_path(const void *a, const void *b)
{
    return strcmp(((const struct disk *)a)->path,
                  ((const struct disk *)b)->path);
}

/* "Flight_Simulator_II_(Disk)_B_-_SD01.d64" -> "B - SD01" */
static void make_label(const char *path, char *out, size_t outsz)
{
    const char *base = strrchr(path, '/');
    const char *p;
    size_t i = 0;

    base = base ? base + 1 : path;
    p = strstr(base, "(Disk)_");
    if (p)
        base = p + 7;
    for (p = base; *p && strcmp(p, ".d64") != 0 && i + 1 < outsz; p++)
        out[i++] = *p == '_' ? ' ' : *p;
    out[i] = 0;
}

int shelf_init(const char *dir, const char *userdisk_path)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    count = 0;
    current = -1;
    if (d) {
        while ((e = readdir(d)) != NULL && count < SHELF_MAX - 1) {
            size_t n = strlen(e->d_name);
            if (n < 5 || strcmp(e->d_name + n - 4, ".d64") != 0 ||
                e->d_name[0] == '.')
                continue;
            sprintf(shelf[count].path, "%.500s/%.500s", dir, e->d_name);
            make_label(shelf[count].path, shelf[count].label,
                       sizeof shelf[count].label);
            shelf[count].writable = 0;
            count++;
        }
        closedir(d);
        qsort(shelf, (size_t)count, sizeof shelf[0], by_path);
    } else {
        fprintf(stderr, "shelf: no disk directory %s\n", dir);
    }
    if (userdisk_path) {
        strncpy(shelf[count].path, userdisk_path,
                sizeof shelf[count].path - 1);
        strcpy(shelf[count].label, "User disk");
        shelf[count].writable = 1;
        count++;
    }
    return count;
}

static int insert(int i)
{
    if (i < 0 || i >= count)
        return -1;
    if (diskio_mount(shelf[i].path, shelf[i].writable) != 0)
        return -1;
    current = i;
    return 0;
}

int shelf_insert_path(const char *path)
{
    int i;
    for (i = 0; i < count; i++)
        if (strcmp(shelf[i].path, path) == 0 ||
            strstr(shelf[i].path, path) != NULL)
            return insert(i);
    return -1;
}

void shelf_step(int dir)
{
    int i, tries;
    if (count == 0)
        return;
    i = current;
    for (tries = 0; tries < count; tries++) {
        i = (i + dir + count) % count;
        if (insert(i) == 0)
            return;
    }
}

const char *shelf_label(void)
{
    return current >= 0 ? shelf[current].label : "empty";
}
