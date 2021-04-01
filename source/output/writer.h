#pragma once

#include <arpa/inet.h>
#include <functional>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "output.h"

constexpr char BUFFER[] = "buffer://";
constexpr char UDP[] = "udp://";

class BufferWriter : public x265::OutputFile
{
public:
    BufferWriter(std::function<int(const unsigned char *data, ssize_t bytes)> writeData = nullptr);
    ~BufferWriter();
    static BufferWriter *construct(std::function<int(const unsigned char *data, ssize_t bytes)> writeData);

    virtual bool isFail() const override { return false; }
    virtual bool needPTS() const override{ return false; }
    virtual void release() override{ delete this; }
    virtual const char* getName() const override{ return "udp"; }
    virtual void setParam(x265_param* param) override{}

    virtual int writeHeaders(const x265_nal* nal, uint32_t nalcount) override;
    virtual int writeFrame(const x265_nal* nal, uint32_t nalcount, x265_picture&) override;
    virtual void closeFile(int64_t largest_pts, int64_t second_largest_pts) override;

protected:
    std::function<int(const unsigned char *data, ssize_t bytes)> _writeData;
};

class UdpWriter : public BufferWriter
{
public:
    UdpWriter();
    ~UdpWriter();
    static UdpWriter *construct(const char* fname);
    bool initialize();
    void setendpoint(const char* destinationIp, unsigned short port);
    ssize_t write(const char *data, ssize_t bytes) const;

    virtual int writeHeaders(const x265_nal* nal, uint32_t nalcount) override;
    virtual int writeFrame(const x265_nal* nal, uint32_t nalcount, x265_picture&) override;
    virtual void closeFile(int64_t largest_pts, int64_t second_largest_pts) override;

protected:
    int sock;
    struct sockaddr_in sa;
    FILE *fp;
};
