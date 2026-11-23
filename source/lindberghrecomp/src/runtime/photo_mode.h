#ifndef HOTD4_PHOTO_MODE_H
#define HOTD4_PHOTO_MODE_H

#include <wincodec.h>

enum { PHOTO_GUIDE_COMPACT = 76, PHOTO_GUIDE_EXPANDED = 151 };

static HRESULT photo_write_png(const wchar_t *path, unsigned width, unsigned height, BYTE *pixels)
{
    HRESULT com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return com;

    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    HRESULT result = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                      &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(result)) result = IWICImagingFactory_CreateStream(factory, &stream);
    if (SUCCEEDED(result)) result = IWICStream_InitializeFromFilename(stream, path, GENERIC_WRITE);
    if (SUCCEEDED(result)) result = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
    if (SUCCEEDED(result)) result = IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(result)) result = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL);
    if (SUCCEEDED(result)) result = IWICBitmapFrameEncode_Initialize(frame, NULL);
    if (SUCCEEDED(result)) result = IWICBitmapFrameEncode_SetSize(frame, width, height);
    if (SUCCEEDED(result)) result = IWICBitmapFrameEncode_SetPixelFormat(frame, &format);
    if (SUCCEEDED(result) && !IsEqualGUID(&format, &GUID_WICPixelFormat24bppBGR))
        result = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    if (SUCCEEDED(result)) result = IWICBitmapFrameEncode_WritePixels(frame, height, width * 3, width * height * 3, pixels);
    if (SUCCEEDED(result)) result = IWICBitmapFrameEncode_Commit(frame);
    if (SUCCEEDED(result)) result = IWICBitmapEncoder_Commit(encoder);

    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IWICStream_Release(stream);
    if (factory) IWICImagingFactory_Release(factory);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}

static int photo_save(GLuint framebuffer, int width, int height)
{
    wchar_t directory[MAX_PATH], temporary[MAX_PATH], path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, directory, MAX_PATH);
    if (!length || length >= MAX_PATH) return 0;
    wchar_t *separator = wcsrchr(directory, L'\\');
    if (!separator) return 0;
    *separator = 0;
    if (wcslen(directory) + 12 >= MAX_PATH) return 0;
    wcscat(directory, L"\\..\\Photos");
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return 0;

    SYSTEMTIME now;
    GetLocalTime(&now);
    if (swprintf(path, MAX_PATH, L"%ls\\HOTD4-%04u%02u%02u-%02u%02u%02u-%03u-%lu.png", directory,
                 (unsigned)now.wYear, (unsigned)now.wMonth, (unsigned)now.wDay, (unsigned)now.wHour,
                 (unsigned)now.wMinute, (unsigned)now.wSecond, (unsigned)now.wMilliseconds,
                 (unsigned long)GetCurrentProcessId()) < 0)
        return 0;

    size_t stride = (size_t)width * 3;
    if (width <= 0 || height <= 0 || stride > UINT_MAX / (unsigned)height) return 0;
    size_t bytes = stride * height;
    BYTE *pixels = malloc(bytes + stride);
    if (!pixels) return 0;
    GLint read, pack;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
    glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    glReadPixels(0, 0, width, height, GL_BGR, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)read);
    glPopClientAttrib();
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)pack);

    BYTE *row = pixels + bytes;
    for (int y = 0; y < height / 2; ++y) {
        BYTE *bottom = pixels + y * stride;
        BYTE *top = pixels + (height - 1 - y) * stride;
        memcpy(row, bottom, stride);
        memcpy(bottom, top, stride);
        memcpy(top, row, stride);
    }
    int saved = 0;
    if (GetTempFileNameW(directory, L"htd", 0, temporary)) {
        HRESULT result = photo_write_png(temporary, (unsigned)width, (unsigned)height, pixels);
        saved = SUCCEEDED(result) && MoveFileW(temporary, path);
        if (!saved) DeleteFileW(temporary);
    }
    free(pixels);
    if (saved) fprintf(stderr, "[photo] saved %dx%d PNG\n", width, height);
    return saved;
}

#endif
