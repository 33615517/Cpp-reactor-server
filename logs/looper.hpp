/*实现异步工作器*/
#ifndef DUALLOG_LOOPER_HPP
#define DUALLOG_LOOPER_HPP

#include "buffer.hpp"
#include <condition_variable>
#include <thread>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
namespace duallog
{
    using Functor = std::function<void(Buffer &)>; // 定义函数对象类型，表示异步工作器的工作函数。
    // 异步缓冲策略：安全模式限制内存使用，非安全模式允许缓冲区持续扩容。
    enum class AsyncType
    {
        ASYNC_SAFE,  // 安全的异步工作器，表示缓冲区满了则阻塞，避免资源耗尽的风险
        ASYNC_UNSAFE // 不考虑资源耗尽的风险，缓冲区满了则无限扩容，适用于极限测试
    };
    // 异步工作器：接收前台线程提交的日志，并由后台线程批量调用回调函数处理。
    class AsyncLooper
    {
    public:
        using ptr = std::shared_ptr<AsyncLooper>;
        // 保存日志处理回调和缓冲策略，同时启动后台消费线程。
        AsyncLooper(const Functor &cb,
                    AsyncType loop_type = AsyncType::ASYNC_SAFE)
            : _callBack(cb),
              _async_type(loop_type),
              _stop(false),
              _thread(&AsyncLooper::threadEntry, this)
        {
        }
        // 析构前停止后台线程，并处理生产缓冲区中剩余的数据。
        ~AsyncLooper()
        {
            stop();
        }
        // 通知后台线程退出，并等待线程执行结束。
        void stop()
        {
            _stop = true;
            _cond_pro.notify_all();
            _cond_con.notify_all();
            if (_thread.joinable())
            {
                _thread.join();
            }
        }
        // 向生产缓冲区追加一段日志数据，并通知后台线程消费。
        void push(const char *data, size_t len)
        {
            {
                std::unique_lock<std::mutex> lock(_mutex);

                if (_async_type == AsyncType::ASYNC_SAFE)
                {
                    _cond_pro.wait(lock, [this, len]
                                   { return _stop ||
                                            _pro_buf.writeAbleSize() >= len ||
                                            (_pro_buf.empty() && len > _pro_buf.writeAbleSize()); });
                }
                if (_stop)
                {
                    return;
                }

                _pro_buf.push(data, len);
            }

            // 已经释放_mutex
            _cond_con.notify_one();
        }

    private:
        // 后台线程入口：交换生产/消费缓冲区，然后批量处理日志数据。
        void threadEntry()
        {
            while (true)
            {
                // 为互斥锁设置一个生命周期，当缓冲区交换完毕后就解锁（并不对数据的处理过程加锁保护）
                {

                    // 1.判断生产缓冲区有没有数据，有则交换，无则阻塞
                    std::unique_lock<std::mutex> lock(_mutex);
                    _cond_con.wait(lock, [this]
                                   { return !_pro_buf.empty() || _stop; });
                    if (_stop && _pro_buf.empty())
                    {
                        break;
                    }

                    _pro_buf.swap(_con_buf);
                    // 2.唤醒生产者线程，通知其缓冲区中有足够的空间可写入数据。
                    _cond_pro.notify_all();
                }
                // 交换缓冲区
                // 3.被唤醒后，对消费者缓冲区中的数据进行处理
                _callBack(_con_buf);

                // 4.初始化消费缓冲区
                // 5.唤醒生产者线程，通知其缓冲区中有足够的空间可写入数据。
                _con_buf.reset();
            }
        }

    private:
        Functor _callBack; // 具体对缓冲区数据进行处理的回调函数，由异步工作器的使用者提供。
    private:
        AsyncType _async_type; // 异步工作器的类型，安全的异步工作器还是不安全的异步工作器。
        std::atomic<bool> _stop;
        Buffer _pro_buf;                   // 生产者缓冲区
        Buffer _con_buf;                   // 消费者缓冲区
        std::mutex _mutex;                 // 互斥锁
        std::condition_variable _cond_pro; // 缓冲区有可写空间时唤醒生产者。
        std::condition_variable _cond_con; // 缓冲区有数据时唤醒消费者。
        std::thread _thread;               // 异步工作器对应的工作线程
    };
}

#endif
