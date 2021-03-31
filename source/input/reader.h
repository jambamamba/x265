#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <string.h>
#include <vector>

#include "frameq.h"

class Reader
{
public:
    Reader(std::function<int(char **data, ssize_t *bytes, int width, int height)> _readRgb888);

    struct Device {
        std::string Name;
    };

    struct CommandLineArs
    {
        Device device;
        std::string Fps;
        int Quality = 80;
    };

    bool ParseDevicesFromCommandLine(int argc, char** argv);
    bool ReadYuvFrame(char *frame, uint32_t framesz, int width, int height);

    FrameQueue queue;
    CommandLineArs args;
    std::function<int(char **data, ssize_t *bytes, int width, int height)> _readRgb888;
};
