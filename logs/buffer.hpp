/*实现异步日志缓冲区*/
#ifndef DUALLOG_BUFFER_HPP
#define DUALLOG_BUFFER_HPP
#include <vector>
#include <iostream>
#include <assert.h>
#include <algorithm> // std::copy
#include <utility>   // std::swap
#include"util.hpp"
namespace duallog
{
#define DEFAULT_BUFFER_SIZE (1 * 1024 * 1024)
#define THRESHOLD_BUFFER_SIZE (8 * 1024 * 1024) // 8MD
#define INCREMENT_BUFFER_SIZE (1 * 1024 * 1024) // 1MB
    // 异步日志缓冲区：保存生产者写入、等待消费者处理的连续日志数据。
    class Buffer
    {
    public:
        // 创建指定初始容量的缓冲区，并将读写位置置零。
        Buffer(size_t size = DEFAULT_BUFFER_SIZE) : _buffer(size), _reader_idx(0), _writer_idx(0) {}

        // 向缓冲区写入数据
        void push(const char *data, size_t len)
        {
            // 缓冲区剩余空间不足时，1.将缓冲区扩容，2.阻塞/返回false
            // 1.固定大小,则直接返回
            /*if(len > writeAbleSize())
            {
                std::cout << "缓冲区剩余空间不足，无法写入数据" << std::endl;
                return;
            }*/
            // 2.动态空间，用于极限测试--扩容
            if (len > writeAbleSize())
            {
                ensureEnoughSize(len);
            }
            // 1.将数据拷贝进缓冲区
            std::copy(data, data + len, &_buffer[_writer_idx]);
            // 2.将当前位置的写指针向后偏移len个字节
            _writer_idx += len;
        }
        // 获取当前可写的字节数
        size_t writeAbleSize() const
        {
            // 对于扩容思路来说，不存在可写空间大小，因为总是可写
            // 因此这个接口仅仅针对固定大小的缓冲区使用
            return _buffer.size() - _writer_idx;
        }
        // 返回可读数据的起始地址
        const char *begin()
        {
            return &_buffer[_reader_idx];
        }
        // 获取当前可读的字节数
        size_t readAbleSize()
        {
            return _writer_idx - _reader_idx;
        }
        // 将读位置向后移动 len 个字节。
        void moveReader(size_t len)
        {
            assert(len <= readAbleSize());
            _reader_idx += len;
        }
        // 重置读写位置，初始化缓冲区
        void reset()
        {
            _reader_idx = 0;
            _writer_idx = 0;
        }
        // 对buffer进行交换操作
        void swap(Buffer &buffer)
        {
            _buffer.swap(buffer._buffer);
            std::swap(_reader_idx, buffer._reader_idx);
            std::swap(_writer_idx, buffer._writer_idx);
        }
        // 判断缓冲区是否为空
        bool empty()
        {
            return readAbleSize() == 0;
        }

    private:
        // 对空间进行扩容操作
        void ensureEnoughSize(size_t len)
        {
            size_t required_size = _writer_idx + len;
            size_t new_size = _buffer.size();

            while (new_size < required_size)
            {
                if (new_size < THRESHOLD_BUFFER_SIZE)
                {
                    new_size *= 2;
                }
                else
                {
                    new_size += INCREMENT_BUFFER_SIZE;
                }
            }

            _buffer.resize(new_size);
        }
        // 读写指针进行向后偏移操作
        void moveWriter(size_t len)
        {
            assert(len <= writeAbleSize());
            _writer_idx += len;
        }

    private:
        std::vector<char> _buffer; // 实际保存日志字节的数据区。
        size_t _reader_idx; // 当前可读的指针--本质下标
        size_t _writer_idx; // 当前可写的指针--本质下标
    };
}

#endif
