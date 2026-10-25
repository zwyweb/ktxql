#include "qlcommon.h"

OSStatus GenerateThumbnailForURL(void *thisInterface, QLThumbnailRequestRef thumbnail, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options, CGSize maxSize);
void CancelThumbnailGeneration(void *thisInterface, QLThumbnailRequestRef thumbnail);

/* Иконка: берём самый маленький mip-уровень, который не меньше maxSize, и не декодируем ничего лишнего. */
OSStatus GenerateThumbnailForURL(void *thisInterface, QLThumbnailRequestRef thumbnail, CFURLRef url, CFStringRef contentTypeUTI, CFDictionaryRef options, CGSize maxSize)
{
    char path[PATH_MAX], img[PATH_MAX];
    if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof path)) return noErr;
    ql_init_resources();
    if (QLThumbnailRequestIsCancelled(thumbnail)) return noErr;

    double m = maxSize.width > maxSize.height ? maxSize.width : maxSize.height;
    int target = m >= 16 ? (int)m : 256;

    if (kql_render(path, KQL_THUMB, (unsigned)target, img, sizeof img, NULL) != KQL_OK) return noErr;
    if (QLThumbnailRequestIsCancelled(thumbnail)) return noErr;

    CGImageSourceRef src = ql_open_image(img);
    if (!src || CGImageSourceGetCount(src) == 0) { if (src) CFRelease(src); return noErr; }

    CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &target);
    const void *keys[] = { kCGImageSourceCreateThumbnailFromImageAlways,
                           kCGImageSourceCreateThumbnailWithTransform,
                           kCGImageSourceThumbnailMaxPixelSize };
    const void *vals[] = { kCFBooleanTrue, kCFBooleanTrue, n };
    CFDictionaryRef o = CFDictionaryCreate(NULL, keys, vals, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CGImageRef image = CGImageSourceCreateThumbnailAtIndex(src, 0, o);
    CFRelease(o); CFRelease(n); CFRelease(src);
    if (!image) return noErr;

    QLThumbnailRequestSetImage(thumbnail, image, NULL);
    CGImageRelease(image);
    return noErr;
}

void CancelThumbnailGeneration(void *thisInterface, QLThumbnailRequestRef thumbnail)
{
    kql_cancel();
}
