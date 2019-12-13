#pragma once

#include <memory>

class FrameQueue
{
public:
    FrameQueue();
    ~FrameQueue();
    void Push(char* data, size_t sz);
    void Pop(char* data, size_t sz);
protected:
    char* Buffer;
    size_t Bytes;
};
