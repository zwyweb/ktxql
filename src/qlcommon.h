#ifndef QLCOMMON_H
#define QLCOMMON_H
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <QuickLook/QuickLook.h>
#include <stdbool.h>
#include <limits.h>
#include <string.h>
#include "ktxcore.h"

/* Говорим ядру, где лежит Contents/Resources (там install.sh кладёт ktx-path.txt). */
static void ql_init_resources(void)
{
    static int done = 0;
    if (done) return;
    done = 1;
    CFBundleRef b = CFBundleGetBundleWithIdentifier(CFSTR("local.ktxquicklook"));
    if (!b) return;
    CFURLRef u = CFBundleCopyResourcesDirectoryURL(b);
    if (!u) return;
    char p[PATH_MAX];
    if (CFURLGetFileSystemRepresentation(u, true, (UInt8 *)p, sizeof p))
        kql_set_resource_dir(p);
    CFRelease(u);
}

static CGImageSourceRef ql_open_image(const char *p)
{
    CFURLRef u = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)p, (CFIndex)strlen(p), false);
    if (!u) return NULL;
    CGImageSourceRef s = CGImageSourceCreateWithURL(u, NULL);
    CFRelease(u);
    return s;
}
#endif
