#pragma once

#include <functional>
#include <string>

class rgbreader;
class RawImageReader
{
public:
    RawImageReader(std::function<int(char **data, ssize_t *bytes, int width, int height)> readRgb888);
    ~RawImageReader();
    bool ReadAsRgb(const char* device, int width, int height);
    bool ReadAsYuv(const char* device, int width, int height);

    char *RGB;
    char *YUV;
    ssize_t RgbSz;
    ssize_t YuvSz;
protected:
    bool ReadDevice(const char* device, int width, int height);
    bool ReadFromFile(const char *filename, int width, int height);
    int BytesPerPixel = 3;//get from ioctl device
    rgbreader *_rgbreader;
};
