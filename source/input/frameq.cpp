#include "frameq.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

FrameQueue::FrameQueue()
    : Buffer(nullptr)
    , Bytes(0)
{}
FrameQueue::~FrameQueue()
{
    if(Buffer) { free(Buffer); }
}
void FrameQueue::Push(char* data, size_t sz)
{
    Buffer = (char*)realloc(Buffer, Bytes + sz);
    memcpy(Buffer + Bytes, data, sz);
    Bytes += sz;
}
void FrameQueue::Pop(char* data, size_t sz)
{
    memcpy(data, Buffer, sz);
    memmove(Buffer, Buffer + sz, Bytes - sz);
    Bytes -= sz;
}
