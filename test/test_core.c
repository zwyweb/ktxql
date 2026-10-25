/* Линуксовый/маковый тест ядра: ./test_core file.ktx2 [thumb|preview] [target_px] */
#include "../src/ktxcore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    kql_mode m = (argc > 2 && !strcmp(argv[2], "thumb")) ? KQL_THUMB : KQL_PREVIEW;
    unsigned t = argc > 3 ? (unsigned)atoi(argv[3]) : 256;
    char out[4096]; kql_info i;
    kql_status s = kql_render(argv[1], m, t, out, sizeof out, &i);
    printf("%s -> %s (%ux%u, mips=%u, level=%u)%s%s\n", argv[1], kql_status_str(s),
           i.width, i.height, i.levels, i.level_used, s == KQL_OK ? " " : "", s == KQL_OK ? out : "");
    return s;
}
