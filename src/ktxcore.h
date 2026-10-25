/* ktxcore: платформо-независимая часть (POSIX). Вызывает `ktx extract`,
 * кэширует результат и ограничивает потребление ресурсов. */
#ifndef KTXCORE_H
#define KTXCORE_H
#include <stddef.h>
#include <stdint.h>

/* ---- Лимиты (можно переопределить через -D при сборке) ---- */
#ifndef KQL_PREVIEW_MAX_EDGE
#define KQL_PREVIEW_MAX_EDGE 2048      /* целимся в mip-уровень с длинной стороной <= 2048 */
#endif
#ifndef KQL_PREVIEW_MAX_MPIX
#define KQL_PREVIEW_MAX_MPIX 24        /* больше этого preview не строим */
#endif
#ifndef KQL_THUMB_MAX_MPIX
#define KQL_THUMB_MAX_MPIX 8           /* больше этого иконку не строим */
#endif
#ifndef KQL_PREVIEW_MAX_FILE_MB
#define KQL_PREVIEW_MAX_FILE_MB 512
#endif
#ifndef KQL_THUMB_MAX_FILE_MB
#define KQL_THUMB_MAX_FILE_MB 128
#endif
#ifndef KQL_PREVIEW_TIMEOUT_S
#define KQL_PREVIEW_TIMEOUT_S 20
#endif
#ifndef KQL_THUMB_TIMEOUT_S
#define KQL_THUMB_TIMEOUT_S 8
#endif
#ifndef KQL_MAX_PARALLEL
#define KQL_MAX_PARALLEL 2             /* одновременно запущенных `ktx` на всю систему */
#endif
#ifndef KQL_CACHE_MAX_MB
#define KQL_CACHE_MAX_MB 256
#endif
#ifndef KQL_CACHE_MAX_FILES
#define KQL_CACHE_MAX_FILES 1500
#endif

typedef enum {
    KQL_OK = 0,
    KQL_ERR_NO_KTX,      /* бинарник ktx не найден */
    KQL_ERR_NOT_KTX2,    /* не KTX2 (например, KTX1) */
    KQL_ERR_TOO_BIG,     /* текстура/файл выше лимитов */
    KQL_ERR_BUSY,        /* не дождались свободного слота */
    KQL_ERR_FAILED,      /* ktx вернул ошибку */
    KQL_ERR_TIMEOUT,     /* ktx убит по таймауту */
    KQL_ERR_CANCELLED
} kql_status;

typedef enum { KQL_PREVIEW = 0, KQL_THUMB = 1 } kql_mode;

typedef struct {
    uint32_t width, height, levels, faces, layers, vk_format, supercompression;
    uint32_t level_used;
} kql_info;

/* Каталог, где лежит ktx-path.txt (Contents/Resources плагина). Можно NULL. */
void        kql_set_resource_dir(const char *dir);
/* Возвращает KQL_OK и путь к готовому изображению (PNG или EXR по содержимому). */
kql_status  kql_render(const char *path, kql_mode mode, unsigned target_px,
                       char *out_path, size_t out_cap, kql_info *info);
void        kql_cancel(void);
const char *kql_status_str(kql_status s);

#endif
