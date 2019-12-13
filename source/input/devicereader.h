#pragma once

#include <string>

class RawImageReader
{
public:
    RawImageReader();
    ~RawImageReader();
    bool ReadAsRgb(const char* device, int width, int height);
    bool ReadAsYuv(const char* device, int width, int height);

    char *RGB;
    char *YUV;
    unsigned long RgbSz;
    unsigned long YuvSz;
protected:
    bool ReadDevice(const char* device, int width, int height);
    int BytesPerPixel = 3;//get from ioctl device
};
