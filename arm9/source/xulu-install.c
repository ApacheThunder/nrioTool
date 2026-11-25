#include "xulu-install.h"
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

enum { PATH_SIZE = 128 };

static bool join(char *out, const char *root, const char *name)
{
    int n = snprintf(out, PATH_SIZE, "%s/%s", root, name);
    return n >= 0 && n < PATH_SIZE;
}

/* 0 means missing, 1 means a regular file, -1 means inaccessible or not a file. */
static int present(const char *path)
{
    struct stat st;
    if (stat(path, &st)) return errno == ENOENT ? 0 : -1;
    return S_ISREG(st.st_mode) ? 1 : -1;
}

enum nrio_xulu_result nrio_install_xulu_files(const char *root, bool allow_switch,
    struct nrio_file_id xulu, struct nrio_file_id stock, nrio_file_source stream,
    unsigned xulu_asset, unsigned stock_asset)
{
    char live[PATH_SIZE], fallback[PATH_SIZE], staged[PATH_SIZE], temp[PATH_SIZE], hold[PATH_SIZE], custom[PATH_SIZE];
    if (!join(live, root, "xmenu.dat") || !join(fallback, root, "xmenu.nds") ||
        !join(staged, root, "xulu.new") || !join(temp, root, "xulu.tmp") ||
        !join(hold, root, "xulu.old") || !join(custom, root, "xmenu-old.nds")) return NRIO_XULU_ERROR;

    int live_exists = present(live), backup_exists = present(fallback);
    if (live_exists < 0 || backup_exists < 0) return NRIO_XULU_ERROR;
    if (live_exists && nrio_file_matches(live, xulu)) return NRIO_XULU_ALREADY;
    if (live_exists && !allow_switch) return NRIO_XULU_UNCHANGED;
    if (present(staged) != 0 || present(temp) != 0 || present(hold) != 0) return NRIO_XULU_ERROR;

    /* The fallback always contains the known stock XMenu. Preserve a
       different old loader under its own name and never overwrite either. */
    bool live_stock = live_exists && nrio_file_matches(live, stock);
    bool duplicate = live_stock && backup_exists;
    if (backup_exists && !nrio_file_matches(fallback, stock)) return NRIO_XULU_ALIAS_CONFLICT;
    if (live_exists && !live_stock && present(custom) != 0) return NRIO_XULU_ALIAS_CONFLICT;

    if (nrio_write_new_file(staged, temp, xulu, stream, &xulu_asset) != NRIO_FILE_OK)
        return NRIO_XULU_ERROR;

    enum nrio_xulu_result result = NRIO_XULU_ERROR;
    const char *saved = live_stock ? (duplicate ? hold : fallback) : custom;
    if (!live_stock && !backup_exists &&
        nrio_write_new_file(fallback, temp, stock, stream, &stock_asset) != NRIO_FILE_OK)
        goto done;
    if (live_exists && rename(live, saved)) goto done;

    if (rename(staged, live)) {
        if (live_exists && rename(saved, live)) result = NRIO_XULU_RESTORE_FAILED;
        goto done;
    }
    return duplicate && unlink(hold) ? NRIO_XULU_OLD_KEPT : NRIO_XULU_OK;

done:
    unlink(staged);
    return result;
}
