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

enum nrio_xulu_result nrio_restore_stock_menu(const char *root,
    struct nrio_file_id legacy_launcher, struct nrio_file_id stock, nrio_file_source stream,
    unsigned stock_asset)
{
    char live[PATH_SIZE], staged[PATH_SIZE], temp[PATH_SIZE], saved[PATH_SIZE];
    if (!join(live, root, "xmenu.dat") || !join(saved, root, "xulu-old.nds") ||
        !join(staged, root, "xmenu.new") || !join(temp, root, "xmenu.tmp")) return NRIO_XULU_ERROR;
    const int has_live = present(live);
    if (has_live < 0) return NRIO_XULU_ERROR;
    if (!has_live) return nrio_write_new_file(live, temp, stock, stream, &stock_asset) == NRIO_FILE_OK
        ? NRIO_XULU_OK : NRIO_XULU_ERROR;
    if (nrio_file_matches(live, stock)) return NRIO_XULU_ALREADY;
    if (!nrio_file_matches(live, legacy_launcher)) return NRIO_XULU_UNCHANGED;
    if (present(saved) != 0 || present(staged) != 0 || present(temp) != 0)
        return NRIO_XULU_ALIAS_CONFLICT;
    if (nrio_write_new_file(staged, temp, stock, stream, &stock_asset) != NRIO_FILE_OK)
        return NRIO_XULU_ERROR;
    if (!nrio_file_matches(live, legacy_launcher) || rename(live, saved)) {
        unlink(staged);
        return NRIO_XULU_ERROR;
    }
    if (rename(staged, live)) {
        unlink(staged);
        return rename(saved, live) ? NRIO_XULU_RESTORE_FAILED : NRIO_XULU_ERROR;
    }
    return NRIO_XULU_OK;
}
