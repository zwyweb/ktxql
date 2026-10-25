#include "qlcommon.h"

OSStatus GeneratePreviewForURL(void *thisInterface, QLPreviewRequestRef preview, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options);
void CancelPreviewGeneration(void *thisInterface, QLPreviewRequestRef preview);

/* Превью: полноразмерная картинка (до ~2048 px по длинной стороне — выбирается mip-уровень),
 * рисуется в bitmap-контекст так же, как в WebPQuickLook. */
OSStatus GeneratePreviewForURL(void *thisInterface, QLPreviewRequestRef preview, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options)
{
    char path[PATH_MAX], img[PATH_MAX];
    if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof path)) return noErr;
    ql_init_resources();
    if (QLPreviewRequestIsCancelled(preview)) return noErr;

    if (kql_render(path, KQL_PREVIEW, 0, img, sizeof img, NULL) != KQL_OK) return noErr;
    if (QLPreviewRequestIsCancelled(preview)) return noErr;

    CGImageSourceRef src = ql_open_image(img);
    CGImageRef image = (src && CGImageSourceGetCount(src) > 0) ? CGImageSourceCreateImageAtIndex(src, 0, NULL) : NULL;
    if (src) CFRelease(src);
    if (!image) return noErr;

    size_t w = CGImageGetWidth(image), h = CGImageGetHeight(image);
    CGContextRef ctx = QLPreviewRequestCreateContext(preview, CGSizeMake((CGFloat)w, (CGFloat)h), true, NULL);
    if (ctx) {
        CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), image);
        QLPreviewRequestFlushContext(preview, ctx);
        CGContextRelease(ctx);
    }
    CGImageRelease(image);
    return noErr;
}

void CancelPreviewGeneration(void *thisInterface, QLPreviewRequestRef preview)
{
    kql_cancel();
}
