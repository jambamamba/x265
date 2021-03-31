#include "rgbreader.h"

rgbreader::rgbreader(std::function<int(char **rgb, ssize_t *bytes, int width, int height)> readRgb888)
    : _readRgb888(readRgb888)
{

}

int rgbreader::readImage(char **rgb, ssize_t *bytes, int width, int height)
{
    return _readRgb888(rgb, bytes, width, height);
}
