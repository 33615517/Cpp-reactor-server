#include <typeinfo>
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <memory>
#include <cstring>
#include <ctime>
#include <any>
#include <functional>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <typeinfo>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include "../logs/duallog.h"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <cstdint>

#define BUFFER_DEFAULT_SIZE 1024

class Buffer
{
private:
    std::vector<char> _buffer;
    uint32_t _read_idx;
    uint32_t _write_idx;

public:
    // ========== 第一组：基础信息 ==========

    Buffer(uint32_t size = BUFFER_DEFAULT_SIZE)
    :_buffer(size),
    _read_idx(0),
    _write_idx(0)
    {}

    void *ReadPosition()
    {
        return  _buffer.data() +_read_idx;
    }
    void *WritePosition()
    {
        return _buffer.data()+_write_idx;
    }

    uint64_t ReadAbleSize()
    {
        return _write_idx - _read_idx;
    }
    uint64_t HeadIdleSize()
    {
        return _read_idx;
    }
    uint64_t TailIdleSize()
    {
        return _buffer.size()-_write_idx;
    }


    // ========== 第二组：读写下标管理 ==========

    void MoveReadOffset(uint64_t len)
    {
        _read_idx+=len;
    }
    void MoveWriteOffset(uint64_t len)
    {
        _write_idx+=len;
    }

    void Clear()
    {
        _read_idx=_write_idx=0;
    }


    // ========== 第三组：空间管理 ==========

    void EnsureWriteSpace(uint64_t len)
    {
        if(TailIdleSize()>=len)return;
        if(_buffer.size()-ReadAbleSize()>=len)
        {
            uint32_t readable = ReadAbleSize();
            std::copy(_buffer.data() + _read_idx, _buffer.data() + _write_idx, _buffer.data());
            _read_idx = 0;
            _write_idx = readable;
        }
        else
        {
            _buffer.resize(_write_idx+len);
        }
    }


    // ========== 第四组：写入功能 ==========

    // 基础写入，不移动下标
    void Write(const void *data, uint64_t len)
    {
        EnsureWriteSpace(len);
        std::copy((const char *)data,(const char *)data+len,_buffer.data()+_write_idx);
    }

    // 基础写入 + 移动下标
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data,len);
        MoveWriteOffset(len);
    }

    // 字符串写入
    void WriteString(const std::string &str)
    {
        Write(str.c_str(),str.size());
    }
    void WriteStringAndPush(const std::string &str)
    {
        WriteAndPush(str.c_str(),str.size());
    }

    // Buffer 写入
    void WriteBuffer(Buffer &buf)
    {
        Write((void *)buf.ReadPosition(), buf.ReadAbleSize());
    }
    void WriteBufferAndPush(Buffer &buf)
    {
        WriteAndPush((void *)buf.ReadPosition(), buf.ReadAbleSize());
    }


    // ========== 第五组：读取功能 ==========

    void Read(void *buf, uint64_t len)
    {
        assert(ReadAbleSize()>=len);
        std::copy((const char *)_buffer.data()+_read_idx,(const char *)_buffer.data()+_read_idx+len,buf);
    }
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf,len);
        MoveReadOffset(len);
    }

    std::string ReadAsString(uint64_t len)
    {
        assert(ReadAbleSize() >= len);
        std::string str(_buffer.data() + _read_idx, len);
        return str;
    }
    std::string ReadAsStringAndPop(uint64_t len)
    {
        std::string str = ReadAsString(len);
        MoveReadOffset(len);
        return str;
    }


    // ========== 第六组：按行读取 ==========

    char *FindCRLF()
    {
        char *res = (char *)memchr(ReadPosition(), '\n', ReadAbleSize());
        return res;
    }

    std::string GetLine()
    {
        char *crlf = FindCRLF();
        if (crlf == nullptr)
        {
            return "";
        }
        return ReadAsString(crlf - (char *)ReadPosition() + 1);
    }
    std::string GetLineAndPop()
    {
         std::string line = GetLine();
        MoveReadOffset(line.size());
        return line;
    }
};

