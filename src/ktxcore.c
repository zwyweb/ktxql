#define _GNU_SOURCE
#include "ktxcore.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char **environ;

#ifdef __APPLE__
#define MTIME_NS(st) ((st).st_mtimespec.tv_nsec)
#else
#define MTIME_NS(st) ((st).st_mtim.tv_nsec)
#endif

static volatile sig_atomic_t g_cancel = 0;
static char g_resdir[PATH_MAX] = "";

void kql_set_resource_dir(const char *dir) {
    if (dir) snprintf(g_resdir, sizeof g_resdir, "%s", dir);
}
void kql_cancel(void) { g_cancel = 1; }

const char *kql_status_str(kql_status s) {
    switch (s) {
    case KQL_OK: return "ok";
    case KQL_ERR_NO_KTX: return "ktx binary not found";
    case KQL_ERR_NOT_KTX2: return "not a KTX2 file";
    case KQL_ERR_TOO_BIG: return "texture/file over limits";
    case KQL_ERR_BUSY: return "no free worker slot";
    case KQL_ERR_FAILED: return "ktx extract failed";
    case KQL_ERR_TIMEOUT: return "ktx extract timed out";
    case KQL_ERR_CANCELLED: return "cancelled";
    }
    return "?";
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
static void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------------ */
/* Поиск бинарника ktx                                                 */
/* ------------------------------------------------------------------ */
static int is_exec(const char *p) {
    struct stat st;
    return p && *p && stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0;
}

static int cache_dir(char *out, size_t cap);

/* Спрашиваем у логин-шелла пользователя, где лежит ktx (его настоящий PATH). Результат кэшируется на диске. */
static int shell_lookup(char *out, size_t cap) {
    struct passwd *pw = getpwuid(getuid());
    const char *sh = (pw && pw->pw_shell && *pw->pw_shell) ? pw->pw_shell : "/bin/zsh";
    int fds[2];
    if (pipe(fds) != 0) return 0;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    char *argv[] = { (char *)sh, "-l", "-i", "-c", "command -v ktx", NULL };
    pid_t pid;
    int rc = posix_spawn(&pid, sh, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) { close(fds[0]); return 0; }

    char buf[PATH_MAX] = ""; size_t len = 0;
    double deadline = now_s() + 4.0;
    for (;;) {
        double left = deadline - now_s();
        if (left <= 0) break;
        fd_set rs; FD_ZERO(&rs); FD_SET(fds[0], &rs);
        struct timeval tv = { (time_t)left, (suseconds_t)((left - (time_t)left) * 1e6) };
        if (select(fds[0] + 1, &rs, NULL, NULL, &tv) <= 0) break;
        ssize_t n = read(fds[0], buf + len, sizeof buf - 1 - len);
        if (n <= 0) break;
        len += (size_t)n;
        if (len >= sizeof buf - 1) break;
    }
    close(fds[0]);
    kill(pid, SIGKILL); waitpid(pid, NULL, 0);
    buf[len] = 0;
    /* берём последнюю непустую строку, начинающуюся с '/' (rc-файлы могут что-то печатать) */
    char *best = NULL;
    for (char *l = strtok(buf, "\r\n"); l; l = strtok(NULL, "\r\n")) if (l[0] == '/') best = l;
    if (!best || !is_exec(best)) return 0;
    snprintf(out, cap, "%s", best);
    return 1;
}

static const char *find_ktx(void) {
    static char buf[PATH_MAX];
    const char *e = getenv("KTXQL_BIN");
    if (is_exec(e)) { snprintf(buf, sizeof buf, "%s", e); return buf; }

    /* путь, записанный install.sh в Contents/Resources/ktx-path.txt */
    if (g_resdir[0]) {
        char cfg[PATH_MAX];
        snprintf(cfg, sizeof cfg, "%s/ktx-path.txt", g_resdir);
        FILE *f = fopen(cfg, "r");
        if (f) {
            char line[PATH_MAX] = "";
            if (fgets(line, sizeof line, f)) {
                size_t n = strlen(line);
                while (n && (line[n-1] == '\n' || line[n-1] == '\r' || line[n-1] == ' ')) line[--n] = 0;
                if (is_exec(line)) { fclose(f); snprintf(buf, sizeof buf, "%s", line); return buf; }
            }
            fclose(f);
        }
    }

    /* QuickLook запускается с урезанным PATH, поэтому перебираем типичные места */
    const char *fixed[] = { "/usr/local/bin/ktx", "/opt/homebrew/bin/ktx", "/opt/local/bin/ktx",
                            "/usr/bin/ktx", NULL };
    for (int i = 0; fixed[i]; i++)
        if (is_exec(fixed[i])) { snprintf(buf, sizeof buf, "%s", fixed[i]); return buf; }

    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        snprintf(buf, sizeof buf, "%s/bin/ktx", pw->pw_dir);
        if (is_exec(buf)) return buf;
    }

    /* последний шанс: путь, найденный ранее через шелл (кэш), либо спросить шелл сейчас */
    char dir[PATH_MAX], cf[PATH_MAX];
    if (cache_dir(dir, sizeof dir) == 0) {
        snprintf(cf, sizeof cf, "%s/ktxpath.txt", dir);
        FILE *f = fopen(cf, "r");
        if (f) {
            char line[PATH_MAX] = "";
            if (fgets(line, sizeof line, f)) {
                size_t n = strlen(line);
                while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
                if (is_exec(line)) { fclose(f); snprintf(buf, sizeof buf, "%s", line); return buf; }
            }
            fclose(f);
        }
        if (shell_lookup(buf, sizeof buf)) {
            f = fopen(cf, "w");
            if (f) { fprintf(f, "%s\n", buf); fclose(f); }
            return buf;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Заголовок KTX2                                                      */
/* ------------------------------------------------------------------ */
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static kql_status read_header(const char *path, kql_info *in, struct stat *st) {
    static const uint8_t magic2[12] = { 0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n' };
    static const uint8_t magic1[12] = { 0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n' };
    int fd = open(path, O_RDONLY);
    if (fd < 0) return KQL_ERR_FAILED;
    if (fstat(fd, st) != 0) { close(fd); return KQL_ERR_FAILED; }
    uint8_t h[64];
    ssize_t n = read(fd, h, sizeof h);
    close(fd);
    if (n < 48) return KQL_ERR_NOT_KTX2;
    memset(in, 0, sizeof *in);
    if (memcmp(h, magic2, 12) == 0) {
        in->vk_format = rd32(h + 12);
        in->width     = rd32(h + 20);
        in->height    = rd32(h + 24);
        in->layers    = rd32(h + 32);
        in->faces     = rd32(h + 36);
        in->levels    = rd32(h + 40);
        in->supercompression = rd32(h + 44);
    } else if (n >= 64 && memcmp(h, magic1, 12) == 0) {
        if (rd32(h + 12) != 0x04030201u) return KQL_ERR_NOT_KTX2;   /* big-endian KTX1 не поддерживаем */
        in->is_ktx1 = 1;
        in->width  = rd32(h + 36);
        in->height = rd32(h + 40);
        in->layers = rd32(h + 48);
        in->faces  = rd32(h + 52);
        in->levels = rd32(h + 56);
    } else {
        return KQL_ERR_NOT_KTX2;
    }
    if (in->width == 0) return KQL_ERR_NOT_KTX2;
    if (in->height == 0) in->height = 1;
    if (in->levels == 0) in->levels = 1;
    return KQL_OK;
}

static uint32_t pick_level(const kql_info *i, kql_mode m, unsigned target) {
    uint32_t longest = i->width > i->height ? i->width : i->height, L = 0;
    if (m == KQL_PREVIEW) {
        while (L + 1 < i->levels && (longest >> L) > KQL_PREVIEW_MAX_EDGE) L++;
    } else {
        if (target < 64) target = 64;
        /* самый маленький уровень, который всё ещё >= размера иконки */
        while (L + 1 < i->levels && (longest >> (L + 1)) >= target) L++;
    }
    return L;
}

/* ------------------------------------------------------------------ */
/* Кэш                                                                 */
/* ------------------------------------------------------------------ */
static int cache_dir(char *out, size_t cap) {
    char base[PATH_MAX] = "";
#ifdef __APPLE__
    size_t n = confstr(_CS_DARWIN_USER_CACHE_DIR, base, sizeof base);
    if (n == 0 || n >= sizeof base) base[0] = 0;
#endif
    if (base[0]) {
        size_t l = strlen(base);
        if (l && base[l-1] == '/') base[l-1] = 0;
        snprintf(out, cap, "%s/ktxquicklook", base);
    } else {
        const char *t = getenv("TMPDIR");
        snprintf(out, cap, "%s/ktxquicklook-%u", (t && *t) ? t : "/tmp", (unsigned)getuid());
    }
    if (mkdir(out, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    while (n--) { h ^= *b++; h *= 1099511628211ULL; }
    return h;
}

typedef struct { char path[PATH_MAX]; time_t mt; off_t sz; } cent;
static int cmp_old(const void *a, const void *b) {
    time_t x = ((const cent *)a)->mt, y = ((const cent *)b)->mt;
    return (x > y) - (x < y);
}

static void evict(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    cent *v = NULL; size_t n = 0, cap = 0;
    long long total = 0;
    time_t now = time(NULL);
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        int img = l > 4 && !strcmp(e->d_name + l - 4, ".img");
        int tmp = l > 4 && !strcmp(e->d_name + l - 4, ".tmp");
        if (!img && !tmp) continue;
        char p[PATH_MAX]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (stat(p, &st) != 0) continue;
        if (tmp) { if (now - st.st_mtime > 600) unlink(p); continue; }
        if (n == cap) { cap = cap ? cap * 2 : 256; v = realloc(v, cap * sizeof *v); if (!v) { closedir(d); return; } }
        snprintf(v[n].path, sizeof v[n].path, "%s", p);
        v[n].mt = st.st_mtime; v[n].sz = st.st_size; total += st.st_size; n++;
    }
    closedir(d);
    long long maxb = (long long)KQL_CACHE_MAX_MB * 1024 * 1024;
    if (total > maxb || n > (size_t)KQL_CACHE_MAX_FILES) {
        qsort(v, n, sizeof *v, cmp_old);
        for (size_t i = 0; i < n && (total > maxb * 3 / 4 || n - i > (size_t)KQL_CACHE_MAX_FILES * 3 / 4); i++) {
            unlink(v[i].path); total -= v[i].sz;
        }
    }
    free(v);
}

/* ------------------------------------------------------------------ */
/* Ограничение параллелизма (межпроцессное) и запуск ktx               */
/* ------------------------------------------------------------------ */
/* >=0 fd со слотом; -1 таймаут/отмена; -2 блокировки недоступны (работаем без них) */
static int acquire_slot(const char *dir, double deadline) {
    for (;;) {
        int opened = 0;
        for (int i = 0; i < KQL_MAX_PARALLEL; i++) {
            char p[PATH_MAX];
            snprintf(p, sizeof p, "%s/slot%d", dir, i);
            int fd = open(p, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
            if (fd < 0) continue;
            opened++;
            if (flock(fd, LOCK_EX | LOCK_NB) == 0) return fd;
            close(fd);
        }
        if (!opened) return -2;
        if (g_cancel || now_s() > deadline) return -1;
        sleep_ms(25);
    }
}

static kql_status run_cmd(const char *exe, char *const argv[], double deadline) {
    const char *ktx = exe;

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawn(&pid, ktx, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return KQL_ERR_FAILED;
    setpriority(PRIO_PROCESS, (id_t)pid, 10);   /* фоновый приоритет, чтобы не лагал Finder */

    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (r < 0 && errno != EINTR) return KQL_ERR_FAILED;
        if (g_cancel || now_s() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return g_cancel ? KQL_ERR_CANCELLED : KQL_ERR_TIMEOUT;
        }
        sleep_ms(5);
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? KQL_OK : KQL_ERR_FAILED;
}

static char g_detail[400] = "";

static kql_status run_ktx(const char *ktx, const char *in, uint32_t level, const char *out, double deadline) {
    char lv[16], errp[PATH_MAX];
    snprintf(lv, sizeof lv, "%u", level);
    snprintf(errp, sizeof errp, "%s.err", out);
    char *argv[] = { (char *)ktx, "extract", "--level", lv, (char *)in, (char *)out, NULL };

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, errp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    pid_t pid;
    int rc = posix_spawn(&pid, ktx, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) {
        snprintf(g_detail, sizeof g_detail, "posix_spawn(%s) failed: %s", ktx, strerror(rc));
        unlink(errp);
        return KQL_ERR_FAILED;
    }
    setpriority(PRIO_PROCESS, (id_t)pid, 10);   /* фоновый приоритет, чтобы не лагал Finder */

    int status = 0;
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (r < 0 && errno != EINTR) { snprintf(g_detail, sizeof g_detail, "waitpid: %s", strerror(errno)); unlink(errp); return KQL_ERR_FAILED; }
        if (g_cancel || now_s() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            unlink(errp);
            return g_cancel ? KQL_ERR_CANCELLED : KQL_ERR_TIMEOUT;
        }
        sleep_ms(5);
    }
    int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) {
        char line[300] = "";
        FILE *f = fopen(errp, "r");
        if (f) {
            size_t n = fread(line, 1, sizeof line - 1, f); line[n] = 0; fclose(f);
            for (char *c = line; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
        }
        if (WIFEXITED(status)) snprintf(g_detail, sizeof g_detail, "ktx exit %d: %s", WEXITSTATUS(status), line);
        else snprintf(g_detail, sizeof g_detail, "ktx killed by signal %d: %s", WIFSIGNALED(status) ? WTERMSIG(status) : -1, line);
    }
    unlink(errp);
    return ok ? KQL_OK : KQL_ERR_FAILED;
}

/* KTX1 -> временный KTX2 через ktx2ktx2 (лежит рядом с ktx) */
static kql_status convert_ktx1(const char *ktx, const char *in, const char *out2, double deadline) {
    char conv[PATH_MAX];
    snprintf(conv, sizeof conv, "%s", ktx);
    char *slash = strrchr(conv, '/');
    if (!slash) return KQL_ERR_NO_KTX;
    snprintf(slash + 1, sizeof conv - (size_t)(slash + 1 - conv), "ktx2ktx2");
    if (!is_exec(conv)) return KQL_ERR_NO_KTX;
    char *argv[] = { conv, "-o", (char *)out2, (char *)in, NULL };
    return run_cmd(conv, argv, deadline);
}

/* ------------------------------------------------------------------ */
static kql_status render_impl(const char *path, kql_mode mode, unsigned target_px,
                      char *out_path, size_t out_cap, kql_info *info_out) {
    g_cancel = 0;
    kql_info inf; struct stat st;
    kql_status s = read_header(path, &inf, &st);
    if (s != KQL_OK) return s;

    int thumb = (mode == KQL_THUMB);
    long long max_file = (long long)(thumb ? KQL_THUMB_MAX_FILE_MB : KQL_PREVIEW_MAX_FILE_MB) * 1024 * 1024;
    unsigned long long max_px = (unsigned long long)(thumb ? KQL_THUMB_MAX_MPIX : KQL_PREVIEW_MAX_MPIX) * 1000000ULL;
    double timeout = thumb ? KQL_THUMB_TIMEOUT_S : KQL_PREVIEW_TIMEOUT_S;

    inf.level_used = pick_level(&inf, mode, target_px);
    if (info_out) *info_out = inf;
    uint64_t lw = inf.width >> inf.level_used, lh = inf.height >> inf.level_used;
    if (!lw) lw = 1;
    if (!lh) lh = 1;
    if (st.st_size > max_file || lw * lh > max_px) return KQL_ERR_TOO_BIG;

    const char *ktx = find_ktx();
    if (!ktx) return KQL_ERR_NO_KTX;

    char dir[PATH_MAX];
    if (cache_dir(dir, sizeof dir) != 0) return KQL_ERR_FAILED;

    uint64_t h = 1469598103934665603ULL;
    int64_t sz = st.st_size, mt = st.st_mtime, mn = MTIME_NS(st);
    h = fnv(h, "v1", 2);
    h = fnv(h, path, strlen(path));
    h = fnv(h, &sz, sizeof sz); h = fnv(h, &mt, sizeof mt); h = fnv(h, &mn, sizeof mn);
    h = fnv(h, &inf.level_used, sizeof inf.level_used);

    char final[PATH_MAX], tmp[PATH_MAX];
    snprintf(final, sizeof final, "%s/%016llx.img", dir, (unsigned long long)h);
    snprintf(tmp, sizeof tmp, "%s/%016llx.%d.tmp", dir, (unsigned long long)h, (int)getpid());

    struct stat cs;
    if (stat(final, &cs) == 0 && cs.st_size > 0) goto hit;

    double deadline = now_s() + timeout;
    int slot = acquire_slot(dir, deadline);
    if (slot == -1) return g_cancel ? KQL_ERR_CANCELLED : KQL_ERR_BUSY;

    /* пока ждали слот, другой процесс мог уже сделать то же самое */
    if (stat(final, &cs) == 0 && cs.st_size > 0) { if (slot >= 0) close(slot); goto hit; }

    if (inf.is_ktx1) {
        char tmp2[PATH_MAX];
        snprintf(tmp2, sizeof tmp2, "%s.k2", tmp);
        s = convert_ktx1(ktx, path, tmp2, deadline);
        if (s == KQL_OK) s = run_ktx(ktx, tmp2, inf.level_used, tmp, deadline);
        unlink(tmp2);
    } else {
        s = run_ktx(ktx, path, inf.level_used, tmp, deadline);
    }
    if (slot >= 0) close(slot);
    if (s == KQL_OK && (stat(tmp, &cs) != 0 || cs.st_size == 0)) s = KQL_ERR_FAILED;
    if (s != KQL_OK) { unlink(tmp); return s; }
    if (rename(tmp, final) != 0) { unlink(tmp); return KQL_ERR_FAILED; }
    evict(dir);

hit:
    utimes(final, NULL);   /* «свежесть» для LRU-вытеснения */
    if (strlen(final) >= out_cap) return KQL_ERR_FAILED;
    strcpy(out_path, final);
    return KQL_OK;
}

/* Журнал: <cache>/ktxquicklook/log.txt — по строке на запрос, чтобы было видно, почему нет превью. */
kql_status kql_render(const char *path, kql_mode mode, unsigned target_px,
                      char *out_path, size_t out_cap, kql_info *info_out) {
    kql_info tmp; if (!info_out) info_out = &tmp;
    memset(info_out, 0, sizeof *info_out);
    double t0 = now_s();
    g_detail[0] = 0;
    kql_status s = render_impl(path, mode, target_px, out_path, out_cap, info_out);
    char dir[PATH_MAX], lf[PATH_MAX];
    if (cache_dir(dir, sizeof dir) == 0) {
        snprintf(lf, sizeof lf, "%s/log.txt", dir);
        struct stat st;
        if (stat(lf, &st) == 0 && st.st_size > 100000) unlink(lf);
        FILE *f = fopen(lf, "a");
        if (f) {
            const char *k = find_ktx();
            fprintf(f, "%ld %s %s -> %s | %ux%u mips=%u level=%u vk=%u scheme=%u | %.2fs | ktx=%s | %s\n",
                    (long)time(NULL), mode == KQL_THUMB ? "thumb" : "preview", path, kql_status_str(s),
                    info_out->width, info_out->height, info_out->levels, info_out->level_used,
                    info_out->vk_format, info_out->supercompression, now_s() - t0, k ? k : "(none)", g_detail);
            fclose(f);
        }
    }
    return s;
}
