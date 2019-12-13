#include "devicereader.h"

#include <algorithm>
#include <iostream>

namespace  {

inline int clamp(int num, int min, int max)
{
    return (num < min) ? min :
                         (num > max) ? max :
                                       num;
}

void makeRgbFile(int width, int height)
{
    FILE* fp = fopen("/home/oosman/Downloads/rgb.data", "wb");
    for(int i= 0; i < height; ++i)
    {
        for(int j= 0; j < width; ++j)
        {
            unsigned char rgb[3] = {0};
            if(j < width/3) rgb[0] = 0xff;
            else if(j < 2*width/3) rgb[1] = 0xff;
            else rgb[2] = 0xff;
            fwrite(rgb, 3, 1, fp);
        }
    }
    fclose(fp);
}
}//namespace

RawImageReader::RawImageReader()
    : RGB(nullptr)
    , YUV(nullptr)
    , RgbSz(0)
{
}

RawImageReader::~RawImageReader()
{
    if(RGB) free(RGB);
    if(YUV) free(YUV);
}

bool RawImageReader::ReadAsRgb(const char* device, int width, int height)
{
//    makeRgbFile(width, height);
    if(!ReadDevice(device, width, height))
    {
        return false;
    }
    for(int i = 0; i < RgbSz; i+=BytesPerPixel)
    {
        char tmp = RGB[i];
        RGB[i] = RGB[i+2];
        RGB[i+2] = tmp;
    }
    return true;
}
bool RawImageReader::ReadAsYuv(const char* device, int width, int height)
{
    if(!ReadDevice(device, width, height))
    {
        return false;
    }
    int frameSize = width * height;
    int chromasize = frameSize;// / 4;
    {

        YuvSz = frameSize * 3;
        YUV = (char*)realloc(YUV, YuvSz);
        if (!YUV)
        {
            std::cout << "Failed to allocate YUV memory for device " << device
                      << "\n" ;
            return false;
        }

        unsigned char *rgb = (unsigned char *)RGB;
        unsigned char *yuv = (unsigned char *)YUV;

        int yIndex = 0;
        int uIndex = frameSize;
        int vIndex = frameSize + chromasize;

        int R, G, B, Y, U, V;
        int index = 0;
        int rgbIndex = 0;

        for (int j = 0; j < height; j++)
        {
            for (int i = 0; i < width; i++)
            {
                R = rgb[rgbIndex++];
                G = rgb[rgbIndex++];
                B = rgb[rgbIndex++];

                Y = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
                U = ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128;
                V = ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128;

                yuv[yIndex++] = (unsigned char)clamp(Y, 0, 255);
                yuv[uIndex++] = (unsigned char)clamp(U, 0, 255);
                yuv[vIndex++] = (unsigned char)clamp(V, 0, 255);

                index++;
            }
        }
    }
    return true;
}
bool RawImageReader::ReadDevice(const char *device, int width, int height)
{
    FILE* fp = fopen(device, "rb");
    if (!fp)
    {
        std::cout << "Failed to open device " << device << "\n" ;
        return false;
    }

    BytesPerPixel = 3;//get from ioctl device
    RgbSz = width * height * BytesPerPixel;

    RGB = (char*)realloc(RGB, RgbSz);
    if (!RGB)
    {
        std::cout << "Failed to allocate memory for device " << device
                  << "\n" ;
        return false;
    }

    fread(RGB, RgbSz, 1, fp);
    fclose(fp);
    return true;
}
