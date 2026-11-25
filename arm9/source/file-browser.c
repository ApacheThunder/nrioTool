#include <nds.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "button-legend.h"
#include "file-browser.h"
#include "file-transfer.h"
#include "name-editor.h"
#include "udisk-setup.h"
#include "ui.h"

enum { PATH_BYTES = 1024, LIST_ROWS = 10, MAX_ENTRIES = 2048 };
struct volume { const char *root, *label; };
struct entry { char *name; bool dir; };
struct listing { struct entry *items; unsigned size; bool incomplete; };

static uint32_t input(void)
{
    for (;;) {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown()) return keysDown();
    }
}

/* The console interprets escape characters. Print only display-safe bytes. */
static const char *display(const char *name, unsigned width)
{
    static char text[33];
    size_t n = strlen(name);
    for (unsigned i = 0; i < width && i < n; ++i) {
        unsigned char c = name[i];
        text[i] = c >= 32 && c < 127 ? c : '?';
    }
    text[n < width ? n : width] = 0;
    if (n > width && width) text[width - 1] = '~';
    return text;
}

static bool path_join(char dest[PATH_BYTES], const char *dir, const char *name)
{
    size_t n = strlen(dir);
    int count = snprintf(dest, PATH_BYTES, "%s%s%s", dir,
                         n && dir[n - 1] != '/' ? "/" : "", name);
    return count >= 0 && count < PATH_BYTES;
}

static void message(const char *s)
{
    consoleClear();
    printf("%s\n\n", s);
    nrio_button_legend("A/B Back", NULL);
    while (!(input() & (KEY_A | KEY_B))) {}
}

static bool confirm(const char *question, const char *name)
{
    consoleClear();
    printf("%s\n%s\n\n", question, display(name, 31));
    nrio_button_legend("A Confirm   B Cancel", NULL);
    for (;;) {
        unsigned key = input();
        if (key & KEY_B) return false;
        if (key & KEY_A) return true;
    }
}

static int by_name(const void *x, const void *y)
{
    const struct entry *a = x, *b = y;
    return a->dir != b->dir ? (a->dir ? -1 : 1) : strcasecmp(a->name, b->name);
}

static void release(struct listing *list)
{
    for (unsigned i = 0; i < list->size; ++i) free(list->items[i].name);
    free(list->items);
    *list = (struct listing){0};
}

static bool read_dir(const char *dir, struct listing *list)
{
    release(list);
    DIR *dp = opendir(dir);
    if (!dp) return false;
    unsigned capacity = 0;
    for (;;) {
        errno = 0;
        struct dirent *it = readdir(dp);
        if (!it) {
            if (errno) list->incomplete = true;
            break;
        }
        if (!strcmp(it->d_name, ".") || !strcmp(it->d_name, "..")) continue;
        if (strlen(it->d_name) >= 256) { list->incomplete = true; continue; }
        if (list->size == MAX_ENTRIES) { list->incomplete = true; break; }
        if (list->size == capacity) {
            unsigned next = capacity ? capacity * 2 : 32;
            void *items = realloc(list->items, next * sizeof(*list->items));
            if (!items) { list->incomplete = true; break; }
            list->items = items;
            capacity = next;
        }
        bool dir_entry = it->d_type == DT_DIR;
        if (!dir_entry && it->d_type != DT_REG) {
            char full[PATH_BYTES];
            struct stat st;
            if (!path_join(full, dir, it->d_name)) { list->incomplete = true; continue; }
            if (stat(full, &st) || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) continue;
            dir_entry = S_ISDIR(st.st_mode);
        }
        char *name = strdup(it->d_name);
        if (!name) { list->incomplete = true; break; }
        list->items[list->size++] = (struct entry){name, dir_entry};
    }
    closedir(dp);
    if (list->size > 1) qsort(list->items, list->size, sizeof(*list->items), by_name);
    return true;
}

static uint64_t free_bytes(const char *path, bool *valid)
{
    struct statvfs st;
    *valid = statvfs(path, &st) == 0;
    if (!*valid) return 0;
    return (uint64_t)st.f_bavail * (st.f_frsize ? st.f_frsize : st.f_bsize);
}

static void print_size(uint64_t bytes)
{
    if (bytes >= (1u << 20)) printf("%lu MiB", (unsigned long)(bytes >> 20));
    else if (bytes >= 1024) printf("%lu KiB", (unsigned long)(bytes >> 10));
    else printf("%lu B", (unsigned long)bytes);
}

enum { MAX_PICK = 32 };
enum queue_mode { PICKING, COPYING, CUTTING };
struct clipboard { char *paths[MAX_PICK]; unsigned count; enum queue_mode mode; };

static const char *basename_of(const char *path) { return strrchr(path, '/') + 1; }

static int find_pick(const struct clipboard *clip, const char *path)
{
    for (unsigned i = 0; i < clip->count; ++i)
        if (!strcasecmp(path, clip->paths[i])) return i;
    return -1;
}

static void drop_pick(struct clipboard *clip, unsigned i)
{
    free(clip->paths[i]);
    --clip->count;
    memmove(clip->paths + i, clip->paths + i + 1, (clip->count - i) * sizeof(clip->paths[0]));
    if (!clip->count) clip->mode = PICKING;
}

static void clear_picks(struct clipboard *clip)
{
    while (clip->count) drop_pick(clip, clip->count - 1);
}

static bool add_pick(struct clipboard *clip, const char *path)
{
    if (clip->count == MAX_PICK) { message("Up to 32 files per batch."); return false; }
    char *copy = strdup(path);
    if (!copy) { message("Not enough memory to mark file."); return false; }
    clip->paths[clip->count++] = copy;
    return true;
}

static void update_picks(struct clipboard *clip, const char *old_path, const char *new_path)
{
    size_t prefix = strlen(old_path);
    for (unsigned i = 0; i < clip->count;) {
        const char *path = clip->paths[i];
        if (!strncasecmp(path, old_path, prefix) && (!path[prefix] || path[prefix] == '/')) {
            if (!new_path) { drop_pick(clip, i); continue; }
            char next[PATH_BYTES];
            int n = snprintf(next, sizeof(next), "%s%s", new_path, path + prefix);
            char *copy = n >= 0 && n < PATH_BYTES ? strdup(next) : NULL;
            if (!copy) { drop_pick(clip, i); continue; }
            free(clip->paths[i]);
            clip->paths[i] = copy;
        }
        ++i;
    }
}

enum action { CANCEL, COPY, CUT, PASTE, CLEAR_CLIPBOARD, RENAME, DELETE };

static enum action item_menu(const struct entry *item, const struct clipboard *clip, bool opened)
{
    struct action_item { enum action value; const char *label; } actions[6];
    unsigned count = 0, selected = 0;
    bool marked = clip->count && clip->mode == PICKING;
    bool queued = clip->count && clip->mode != PICKING;
    if (marked || (!queued && item && !item->dir)) {
        actions[count++] = (struct action_item){COPY, marked ? "Copy marked files" : "Copy"};
        actions[count++] = (struct action_item){CUT, marked ? "Cut marked files" : "Cut"};
    }
    if (queued && opened) actions[count++] = (struct action_item){PASTE, "Paste here"};
    if (clip->count) actions[count++] = (struct action_item){CLEAR_CLIPBOARD,
                            queued ? "Cancel clipboard" : "Clear marks"};
    if (item) {
        actions[count++] = (struct action_item){RENAME, "Rename"};
        actions[count++] = (struct action_item){DELETE, "Delete"};
    }
    if (!count) {
        message("No file or clipboard item.");
        return CANCEL;
    }
    for (;;) {
        consoleClear();
        printf("Actions\n%s\n\n", item ? display(item->name, 31) : "(this folder)");
        for (unsigned i = 0; i < count; ++i)
            printf("%c %s\n\n", i == selected ? '>' : ' ', actions[i].label);
        nrio_button_legend("Up/Down  A Select  B Back", NULL);
        unsigned key = input();
        if (key & KEY_B) return CANCEL;
        if (key & KEY_A) return actions[selected].value;
        if (key & KEY_UP) selected = selected ? selected - 1 : count - 1;
        else if (key & KEY_DOWN) selected = (selected + 1) % count;
    }
}

struct transfer_status { unsigned file, count, last, phase; const char *name; };

static bool progress(void *context, bool verify, uint64_t done, uint64_t total)
{
    struct transfer_status *p = context;
    unsigned percent = total ? (unsigned)(done * 100 / total) : 100;
    if (!done || percent != p->last || (unsigned)verify != p->phase) {
        p->last = percent;
        p->phase = verify;
        consoleClear();
        printf("File %u of %u  %s\n\n%s\n\n%3u%% [", p->file, p->count,
               verify ? "Verifying" : "Copying", display(p->name, 31), percent);
        for (unsigned i = 0; i < 20; ++i) putchar(i < percent / 5 ? '#' : '.');
        puts("]\n");
        nrio_button_legend("B Stop batch", NULL);
    }
    scanKeys();
    return !(keysHeld() & KEY_B);
}

static void show_path(const char *path)
{
    /* Fit a deep path without hiding its final directory. */
    size_t n = strlen(path);
    if (n > 31) printf("~%s\n", display(path + n - 30, 30));
    else puts(display(path, 31));
}

static bool confirm_paste(const char *dir, const struct clipboard *clip, const struct volume *volumes,
                          unsigned count, const char *dest_label)
{
    uint64_t total = 0;
    bool all_sizes = true, one_folder = true;
    const char *first = clip->paths[0];
    size_t parent_len = basename_of(first) - first;
    for (unsigned i = 0; i < clip->count; ++i) {
        const char *path = clip->paths[i];
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) all_sizes = false;
        else total += st.st_size;
        if ((size_t)(basename_of(path) - path) != parent_len ||
            strncasecmp(first, path, parent_len)) one_folder = false;
    }
    bool known;
    uint64_t free = free_bytes(dir, &known);
    const char *source_label = "Source";
    for (unsigned i = 0; i < count; ++i)
        if (!strncasecmp(first, volumes[i].root, strlen(volumes[i].root)))
            source_label = volumes[i].label;
    consoleClear();
    printf("%s %u file%s\n\n", clip->mode == CUTTING ? "Move" : "Copy",
           clip->count, clip->count == 1 ? "" : "s");
    if (clip->count == 1) printf("%s\n\n", display(basename_of(first), 31));
    else if (all_sizes) { print_size(total); puts(" total\n"); }
    if (one_folder) {
        char source[PATH_BYTES];
        memcpy(source, first, parent_len);
        source[parent_len] = 0;
        printf("From %s\n", source_label);
        show_path(source);
    } else puts("From multiple folders");
    printf("\nTo %s\n", dest_label);
    show_path(dir);
    if (known) { print_size(free); puts(" free"); }
    putchar('\n');
    nrio_button_legend("A Paste   B Cancel", NULL);
    for (;;) {
        unsigned key = input();
        if (key & KEY_B) return false;
        if (key & KEY_A) return true;
    }
}

enum conflict { STOP, SKIP, KEEP_BOTH, REPLACE };
static enum conflict conflict_menu(const char *name, bool replace_ok)
{
    unsigned pos = 0, count = replace_ok ? 3 : 2;
    static const char *const labels[] = {"Skip", "Keep both", "Replace"};
    for (;;) {
        consoleClear();
        printf("Destination already exists\n%s\n\n", display(name, 31));
        for (unsigned i = 0; i < count; ++i) printf("%c %s\n\n", pos == i ? '>' : ' ', labels[i]);
        nrio_button_legend("A Select   B Stop batch", NULL);
        unsigned key = input();
        if (key & KEY_B) return STOP;
        if (key & KEY_A) return (enum conflict)(pos + 1);
        if (key & KEY_UP) pos = (pos + count - 1) % count;
        else if (key & KEY_DOWN) pos = (pos + 1) % count;
    }
}

static bool unique_path(char dest[PATH_BYTES], const char *dir, const char *name)
{
    const char *ext = strrchr(name, '.');
    if (!ext || ext == name) ext = name + strlen(name);
    for (unsigned i = 2; i < 1000; ++i) {
        char suffix[8], candidate[256];
        snprintf(suffix, sizeof(suffix), " (%u)", i);
        size_t space = sizeof(candidate) - 1 - strlen(suffix), ext_size = strlen(ext);
        if (ext_size >= space) return false;
        size_t stem = ext - name;
        if (stem > space - ext_size) stem = space - ext_size;
        while (stem && ((unsigned char)name[stem] & 0xc0) == 0x80) --stem;
        snprintf(candidate, sizeof(candidate), "%.*s%s%s", (int)stem, name, suffix, ext);
        if (!path_join(dest, dir, candidate)) return false;
        struct stat st;
        if (stat(dest, &st)) return errno == ENOENT;
    }
    return false;
}

static void transfer_summary(const char *summary, char done[][32], unsigned count)
{
    bool show_files = false;
    unsigned first = 0;
    for (;;) {
        consoleClear();
        if (show_files) {
            puts("Completed files\n");
            for (unsigned i = first; i < count && i < first + 10; ++i) puts(done[i]);
            putchar('\n');
            nrio_button_legend("Up/Down Scroll  X Summary", "A/B Back");
        } else {
            printf("%s\n\n", summary);
            nrio_button_legend(count ? "X Completed files  A/B Back" : "A/B Back", NULL);
        }
        unsigned key = input();
        if (key & (KEY_A | KEY_B)) return;
        if ((key & KEY_X) && count) show_files = !show_files;
        else if (show_files && (key & KEY_UP) && first) --first;
        else if (show_files && (key & KEY_DOWN) && first + 10 < count) ++first;
    }
}

static bool paste(const struct volume *volumes, unsigned count, unsigned volume,
                  const char *dir, struct clipboard *clip)
{
    if (!confirm_paste(dir, clip, volumes, count, volumes[volume].label)) return false;
    unsigned total = clip->count, i = 0, completed = 0, skipped = 0, failed = 0, warnings = 0;
    bool stopped = false;
    char done_names[MAX_PICK][32];
    while (completed + skipped + failed < total) {
        const char *source = clip->paths[i];
        const char *name = basename_of(source);
        char dest[PATH_BYTES];
        struct stat st;
        if (!path_join(dest, dir, name)) { ++failed; ++i; continue; }
        bool replace = false;
        bool same_file = !strcasecmp(dest, source);
        bool exists = stat(dest, &st) == 0;
        if (same_file && clip->mode == CUTTING) { ++skipped; ++i; continue; }
        if (exists || same_file) {
            bool other_source = find_pick(clip, dest) >= 0;
            enum conflict choice = conflict_menu(name, exists && S_ISREG(st.st_mode) && !other_source);
            if (choice == STOP) { stopped = true; break; }
            if (choice == SKIP) { ++skipped; ++i; continue; }
            if (choice == KEEP_BOTH && !unique_path(dest, dir, name)) { ++failed; ++i; continue; }
            replace = choice == REPLACE;
        } else if (errno != ENOENT) { ++failed; ++i; continue; }
        struct transfer_status status = {completed + skipped + failed + 1, total, 0, 0, name};
        if (!progress(&status, false, 0, 1)) { stopped = true; break; }
        enum nrio_file_result result = nrio_file_transfer(source, dest, clip->mode == CUTTING,
                                                         replace, progress, &status);
        if (result == NRIO_FILE_CANCELLED) { stopped = true; break; }
        if (result == NRIO_FILE_OK || result == NRIO_FILE_SOURCE_KEPT || result == NRIO_FILE_OLD_KEPT) {
            if (result == NRIO_FILE_SOURCE_KEPT) {
                ++warnings;
                message("Copy verified. Could not\nremove the source.");
            } else if (result == NRIO_FILE_OLD_KEPT) {
                ++warnings;
                message("Copy verified. Old file kept\nas NRIOxxxx.BAK.");
            }
            strcpy(done_names[completed], display(basename_of(dest), 31));
            ++completed;
            drop_pick(clip, i);
        } else {
            ++failed;
            ++i;
            if (result == NRIO_FILE_RESTORE_FAILED) {
                message("Could not finish replace.\nOld file is NRIOxxxx.BAK.");
                stopped = true;
                break;
            }
        }
    }
    char summary[160];
    snprintf(summary, sizeof(summary),
             "%s\n\nCompleted: %u of %u\nSkipped:   %u\nFailed:    %u\nWarnings:  %u\nStill queued: %u",
             stopped ? "Transfer stopped" : "Transfer finished", completed, total,
             skipped, failed, warnings, clip->count);
    transfer_summary(summary, done_names, completed);
    return true;
}

static void parent(char *dir)
{
    char *slash = strrchr(dir, '/');
    if (!slash || !slash[1]) return;
    if (slash > dir && slash[-1] == ':') slash[1] = 0;
    else *slash = 0;
}

static const char *try_mount_cart(void)
{
    consoleClear();
    puts("Checking N-Card filesystem...");
    switch (nrioTool_check_cart_filesystem()) {
        case NRIO_CART_FS_PROBE_FAILED: return "N-Card NAND did not respond.";
        case NRIO_CART_FS_NO_MAP: return "No usable N-Card FAT map.";
        default: break;
    }
    return nrioTool_mount_setup_cart() ? NULL : "N-Card FAT mount failed.";
}

static void browse(struct volume *volumes, unsigned count, const char *cart_problem,
                   bool *mounted_here)
{
    struct listing list = {0};
    /* Keep the three path buffers off the DS system stack. */
    struct clipboard clip = {0};
    static char dirs[3][PATH_BYTES];
    char names[3][256] = {{0}};
    unsigned cursors[3] = {0};
    for (unsigned i = 0; i < count; ++i) strcpy(dirs[i], volumes[i].root);
    char first_dir[PATH_BYTES];
    struct stat st;
    if (path_join(first_dir, volumes[0].root, "nrioFiles") &&
        stat(first_dir, &st) == 0 && S_ISDIR(st.st_mode))
        strcpy(dirs[0], first_dir);
    unsigned vol = 0, selected = 0;
    bool reload = true, opened = false, space_ok = false;
    uint64_t free = 0;
    for (;;) {
        char *dir = dirs[vol];
        if (reload) {
            opened = read_dir(dir, &list);
            free = free_bytes(volumes[vol].root, &space_ok);
            if (names[vol][0]) {
                for (unsigned i = 0; i < list.size; ++i)
                    if (!strcasecmp(list.items[i].name, names[vol])) { selected = i; break; }
            }
            if (selected >= list.size) selected = list.size ? list.size - 1 : 0;
            reload = false;
        }
        unsigned start = selected < LIST_ROWS ? 0 : selected - LIST_ROWS + 1;
        consoleClear();
        for (unsigned i = 0; i < count; ++i)
            printf(i == vol ? "[%s] " : " %s  ", volumes[i].label);
        if (cart_problem) printf(" N-Card!");
        putchar('\n');
        show_path(dir);
        for (unsigned i = 0; i < LIST_ROWS; ++i) {
            unsigned n = start + i;
            if (n < list.size) {
                const struct entry *entry = &list.items[n];
                char full[PATH_BYTES];
                bool marked = clip.count && path_join(full, dir, entry->name) && find_pick(&clip, full) >= 0;
                bool queued = marked && clip.mode != PICKING;
                printf("%c%c ", n == selected ? '>' : ' ', marked ? '*' : entry->dir ? '/' : ' ');
                if (queued) BottomConsoleQueuedColor(true);
                puts(display(entry->name, 28));
                if (queued) BottomConsoleQueuedColor(false);
            } else if (!i) puts(opened ? "(empty)" : "Could not open folder");
            else putchar('\n');
        }
        if (cart_problem) puts("N-Card offline. L/R Retry");
        else if (list.incomplete) puts("Listing limited");
        else if (space_ok) { print_size(free); puts(" free"); }
        else putchar('\n');
        bool queued = clip.count && clip.mode != PICKING;
        if (clip.count) {
            if (queued) BottomConsoleQueuedColor(true);
            if (clip.count == 1)
                printf("%s %s\n", clip.mode == CUTTING ? "Cut:" : queued ? "Copy:" : "Mark:",
                       display(basename_of(clip.paths[0]), 25));
            else printf("%u files %s\n", clip.count, clip.mode == CUTTING ? "queued to move" : queued ? "queued to copy" : "marked");
            if (queued) BottomConsoleQueuedColor(false);
        } else putchar('\n');
        nrio_button_legend(count > 1 || cart_problem ? "L/R Drive  A Open  B Back" : "A Open   B Back",
                           queued ? "X Actions Y Paste START New" : "X Actions SEL Mark ST New");
        unsigned key = input();
        if (key & KEY_B) {
            if (!strcmp(dir, volumes[vol].root)) break;
            parent(dir);
            names[vol][0] = 0;
            selected = 0;
            reload = true;
        } else if (key & (KEY_L | KEY_R)) {
            cursors[vol] = selected;
            if (list.size) strcpy(names[vol], list.items[selected].name);
            else names[vol][0] = 0;
            if (cart_problem) {
                cart_problem = try_mount_cart();
                if (cart_problem) {
                    message(cart_problem);
                    continue;
                }
                *mounted_here = true;
                volumes[count] = (struct volume){"cart:/", "N-Card"};
                strcpy(dirs[count], volumes[count].root);
                vol = count++;
            } else if (count > 1) {
                vol = (vol + count + (key & KEY_L ? count - 1 : 1)) % count;
            } else continue;
            selected = cursors[vol];
            reload = true;
        } else if (key & KEY_Y && queued && opened) {
            reload = paste(volumes, count, vol, dir, &clip);
        } else if (key & KEY_SELECT && list.size && !list.items[selected].dir) {
            if (queued) { message("Paste or cancel the queue first."); continue; }
            char path[PATH_BYTES];
            if (path_join(path, dir, list.items[selected].name)) {
                int mark = find_pick(&clip, path);
                if (mark >= 0) drop_pick(&clip, (unsigned)mark);
                else add_pick(&clip, path);
            }
        } else if (key & KEY_START && opened) {
            char name[256] = "", path[PATH_BYTES];
            if (nrio_edit_name("New folder", name)) {
                bool valid = path_join(path, dir, name);
                if (!valid || mkdir(path, 0777)) message("Could not create folder.");
                reload = true;
            }
        } else if (key & KEY_X) {
            const struct entry *item = list.size ? &list.items[selected] : NULL;
            char path[PATH_BYTES];
            if (item && !path_join(path, dir, item->name)) item = NULL;
            enum action action = item_menu(item, &clip, opened);
            switch (action) {
                case COPY: case CUT:
                    if (clip.count || add_pick(&clip, path))
                        clip.mode = action == CUT ? CUTTING : COPYING;
                    break;
                case PASTE:
                    reload = paste(volumes, count, vol, dir, &clip);
                    break;
                case CLEAR_CLIPBOARD:
                    clear_picks(&clip);
                    break;
                case RENAME: {
                    char name[256], renamed[PATH_BYTES];
                    strcpy(name, item->name);
                    if (!nrio_edit_name("Rename", name) || !strcmp(name, item->name)) break;
                    struct stat st;
                    if (!path_join(renamed, dir, name) ||
                        !stat(renamed, &st) || errno != ENOENT || rename(path, renamed)) {
                        message("Could not rename. The name\nmay already exist.");
                        break;
                    }
                    update_picks(&clip, path, renamed);
                    strcpy(names[vol], name);
                    reload = true;
                    break;
                }
                case DELETE:
                    if (confirm(item->dir ? "Delete empty folder?" : "Delete file?", item->name)) {
                        if (item->dir ? rmdir(path) : unlink(path)) message("Could not delete. Folders\nmust be empty.");
                        else {
                            update_picks(&clip, path, NULL);
                            reload = true;
                        }
                    }
                    break;
                default: break;
            }
        } else if (list.size) {
            const struct entry *item = &list.items[selected];
            char path[PATH_BYTES];
            if (!path_join(path, dir, item->name)) continue;
            if (key & KEY_UP) selected = selected ? selected - 1 : list.size - 1;
            else if (key & KEY_DOWN) selected = (selected + 1) % list.size;
            else if (key & KEY_LEFT) selected = selected >= LIST_ROWS ? selected - LIST_ROWS : 0;
            else if (key & KEY_RIGHT) selected = selected + LIST_ROWS < list.size ? selected + LIST_ROWS : list.size - 1;
            else if (key & KEY_A) {
                if (item->dir) {
                    strcpy(dir, path);
                    names[vol][0] = 0;
                    selected = 0;
                    reload = true;
                } else {
                    struct stat st;
                    if (!stat(path, &st)) {
                        char info[80];
                        snprintf(info, sizeof(info), "%s\n%lu bytes", display(item->name, 31), (unsigned long)st.st_size);
                        message(info);
                    }
                }
            }
        }
    }
    release(&list);
    clear_picks(&clip);
}

void nrio_file_browser(bool sd, bool slot2, bool slot1_fat)
{
    SelectBotConsole();
    struct volume volumes[3];
    unsigned count = 0;
    bool mounted_here = false;
    const char *cart_problem = NULL;
    if (sd) volumes[count++] = (struct volume){"sd:/", "SD Card"};
    if (slot2) volumes[count++] = (struct volume){"fat:/", "Slot-2 storage"};
    if (slot1_fat) volumes[count++] = (struct volume){"fat:/", "N-Card"};
    else {
        cart_problem = try_mount_cart();
        mounted_here = cart_problem == NULL;
        if (mounted_here) volumes[count++] = (struct volume){"cart:/", "N-Card"};
    }
    if (count) browse(volumes, count, cart_problem, &mounted_here);
    else message(cart_problem ? cart_problem : "No mounted storage.");
    if (mounted_here) nrioTool_unmount_setup_cart();
}
