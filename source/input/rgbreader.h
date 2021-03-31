#pragma once

#include <functional>

class rgbreader
{
public:
    rgbreader(std::function<int (char **data, ssize_t *bytes, int width, int height)> readRgb888);
    virtual ~rgbreader(){}
    virtual int readImage(char **rgb, ssize_t *bytes, int width, int height);

protected:
    std::function<int (char **data, ssize_t *bytes, int width, int height)> _readRgb888;
};

