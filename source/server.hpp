#ifndef __M_SERVER_H__
#define __M_SERVER_H__
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

/*
 * Reactor 服务器核心组件：
 *
 *   Socket/TimerFd/EventFd 产生事件
 *              ↓
 *           epoll(Poller)
 *              ↓
 *            Channel
 *              ↓
 *      Eventloop 分发并执行回调
 *              ↓
 *          Connection 处理连接
 *
 * 每个 Eventloop 应由固定线程驱动；其他线程需要通过任务队列和 eventfd
 * 把操作投递给该线程，避免并发修改 Poller、Channel 和 Connection 的状态。
 * TcpServer 的主循环负责接收连接和维护连接表，工作循环负责连接的 I/O
 * 与业务回调；未配置工作线程时，两类工作都由主循环完成。
 */

// Buffer 的默认初始容量，空间不足时会自动扩容。
#define BUFFER_DEFAULT_SIZE 1024

// 可自动扩容的字节缓冲区。
// [_read_idx, _write_idx) 是可读区；其余部分是已经消费或尚未使用的空间。
// 读写指针只用于临时访问，扩容或整理数据后需要重新获取；该类本身不加锁。
class Buffer
{
private:
    std::vector<char> _buffer; // 实际存放数据的连续内存
    uint32_t _read_idx;        // 当前读位置
    uint32_t _write_idx;       // 当前写位置

public:
    // 创建指定初始容量的缓冲区。
    Buffer(uint32_t size = BUFFER_DEFAULT_SIZE)
        : _buffer(size), _read_idx(0), _write_idx(0)
    {
    }

    // 获取当前可写位置。
    void *WritePosition()
    {
        return _buffer.data() + _write_idx;
    }

    // 获取当前可读位置。
    char *ReadPosition()
    {
        return _buffer.data() + _read_idx;
    }

    // 获取写下标到缓冲区末尾之间的连续可写空间大小。
    uint64_t TailIdleSize()
    {
        return _buffer.size() - _write_idx;
    }

    // 获取缓冲区开头到读下标之间的可回收空间大小。
    uint64_t HeadIdleSize()
    {
        return _read_idx;
    }

    // 获取当前尚未消费的数据长度。
    uint64_t ReadAbleSize()
    {
        return _write_idx - _read_idx;
    }

    // 消费 len 字节数据，并将读下标向后移动。
    void MoveReadOffset(uint64_t len)
    {
        if (len == 0)
        {
            return;
        }
        assert(ReadAbleSize() >= len);
        _read_idx += len;
    }

    // 确认已经写入 len 字节，并将写下标向后移动。
    void MoveWriteOffset(uint64_t len)
    {
        assert(TailIdleSize() >= len);
        _write_idx += len;
    }

    // 确保尾部至少有 len 字节连续空间。
    // 总空闲空间足够时整理已有数据，否则直接扩容。
    void EnsureWriteSpace(uint64_t len)
    {
        if (TailIdleSize() >= len)
        {
            return;
        }
        else if (TailIdleSize() + HeadIdleSize() < len)
        {
            // 总空闲空间不足，保留现有读写位置并扩大尾部空间。
            _buffer.resize(_write_idx + len);
        }
        else
        {
            // 将未消费的数据移到开头，回收读下标之前的空间。
            uint32_t readable = ReadAbleSize();
            std::copy(_buffer.data() + _read_idx, _buffer.data() + _write_idx, _buffer.data());
            _read_idx = 0;
            _write_idx = readable;
        }
    }

    // 将数据复制到当前写位置，但不移动写下标。
    // 调用者完成写入后还需要调用 MoveWriteOffset()。
    void Write(const void *data, uint64_t len)
    {
        EnsureWriteSpace(len);
        std::copy((const char *)data, (const char *)data + len, _buffer.data() + _write_idx);
        // MoveWriteOffset(len);
    }

    // 写入字符串，但不移动写下标。
    void WriteString(const std::string &str)
    {
        Write((void *)str.c_str(), str.size());
    }

    // 写入另一个缓冲区的可读数据，但不移动写下标。
    void WriteBuffer(Buffer &buf)
    {
        Write((void *)buf.ReadPosition(), buf.ReadAbleSize());
    }

    // 写入原始数据，并移动写下标。
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data, len);
        MoveWriteOffset(len);
    }

    // 写入字符串，并移动写下标。
    void WriteStringAndPush(const std::string &str)
    {
        WriteString(str);
        MoveWriteOffset(str.size());
    }

    // 写入另一个缓冲区的数据，并移动写下标。
    void WriteBufferAndPush(Buffer &buf)
    {
        WriteBuffer(buf);
        MoveWriteOffset(buf.ReadAbleSize());
    }

    // 从当前读位置复制 len 字节，但不移动读下标。
    void Read(void *buf, uint64_t len)
    {
        assert(ReadAbleSize() >= len);
        std::copy(_buffer.data() + _read_idx, _buffer.data() + _read_idx + len, (char *)buf);
        // MoveReadOffset(len);
    }

    // 读取数据，并移动读下标。
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }

    // 将指定长度的数据读取为字符串，但不移动读下标。
    std::string ReadAsString(uint64_t len)
    {
        assert(ReadAbleSize() >= len);
        std::string str(_buffer.data() + _read_idx, len);
        // MoveReadOffset(len);
        return str;
    }

    // 将指定长度的数据读取为字符串，并移动读下标。
    std::string ReadAsStringAndPop(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        std::string str = ReadAsString(len);
        MoveReadOffset(len);
        return str;
    }

    // 在可读区中查找 '\n'；名称虽为 CRLF，但不要求前一个字符是 '\r'。
    // 找不到换行符时返回 nullptr，供上层判断一行数据是否接收完整。
    char *FindCRLF()
    {
        char *res = (char *)memchr(ReadPosition(), '\n', ReadAbleSize());
        return res;
    }

    // 获取包含末尾换行符的一行数据，不移动读下标；不足一行时返回空字符串。
    std::string GetLine()
    {
        char *crlf = FindCRLF();
        if (crlf == nullptr)
        {
            return "";
        }
        return ReadAsString(crlf - (char *)ReadPosition() + 1);
    }

    // 获取一行数据，并移动读下标。
    std::string GetLineAndPop()
    {
        std::string line = GetLine();
        MoveReadOffset(line.size());
        return line;
    }
    // 逻辑清空缓冲区，不释放已经申请的内存。
    void Clear()
    {
        _read_idx = 0;
        _write_idx = 0;
    }
};

// listen() 等待队列的默认上限。
#define MAX_LISTEN 1024

// TCP socket 的轻量封装，负责文件描述符的创建、连接、收发和关闭。
// _sockfd 的所有权由 Socket 持有，对象销毁时会关闭该描述符。
class Socket
{
private:
    int _sockfd; // 当前 socket 的文件描述符，-1 表示无效

public:
    // 创建一个暂未绑定文件描述符的 Socket 对象。
    Socket() : _sockfd(-1) {}

    // 使用已有文件描述符创建 Socket 对象。
    Socket(int sockfd) : _sockfd(sockfd) {}

    // 销毁对象时关闭所管理的文件描述符。
    ~Socket() { close(_sockfd); }

    // 获取底层文件描述符。
    int Fd() const { return _sockfd; }

    // 创建一个 IPv4 TCP 套接字，成功返回 true。
    bool Create()
    {
        _sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (_sockfd == -1)
        {
            DUALLOG_ERROR("socket create failed: %s", strerror(errno));
            return false;
        }
        return true;
    }
    // 将套接字绑定到指定 IPv4 地址和端口。
    bool Bind(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        if (bind(_sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
            DUALLOG_ERROR("bind failed: %s", strerror(errno));
            return false;
        }
        return true;
    }
    // 将套接字切换为监听状态，backlog 指定等待连接队列的大小。
    bool Listen(int backlog = MAX_LISTEN)
    {
        if (listen(_sockfd, backlog) == -1)
        {
            DUALLOG_ERROR("listen failed: %s", strerror(errno));
            return false;
        }
        return true;
    }
    // 使用当前套接字向指定服务器发起连接。
    bool Connect(const std::string &ip, uint16_t port)
    {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip.c_str());
        if (connect(_sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
            DUALLOG_ERROR("connect failed: %s", strerror(errno));
            return false;
        }
        return true;
    }
    // 从监听队列接收一个连接，成功返回新连接 fd，失败返回 -1。
    int Accept()
    {
        int newfd = accept(_sockfd, nullptr, nullptr);
        if (newfd == -1)
        {
            DUALLOG_ERROR("accept failed: %s", strerror(errno));
            return -1;
        }
        return newfd;
    }
    // 接收数据：正值表示字节数；EAGAIN/EINTR 按当前实现返回 0，其他异常返回 -1。
    // recv() 返回 0 时也进入异常分支，当前实现没有单独区分对端关闭。
    ssize_t Recv(void *buf, size_t len, int flags = 0)
    {
        ssize_t ret = recv(_sockfd, buf, len, flags);
        if (ret <= 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            else
            {
                //DUALLOG_ERROR("recv failed: %s", strerror(errno));
                return -1;
            }
        }
        return ret;
    }

    // 以非阻塞方式接收数据。
    ssize_t NonBlockRecv(void *buf, size_t len)
    {
        return Recv(buf, len, MSG_DONTWAIT);
    }

    // 发送数据：返回实际字节数；暂时不可写或被信号中断返回 0；失败返回 -1。
    ssize_t Send(const void *buf, size_t len, int flags = 0)
    {
        // 向已关闭的连接发送时抑制 SIGPIPE，让调用者通过返回值处理错误。
        ssize_t ret = send(_sockfd, buf, len, flags | MSG_NOSIGNAL);
        if (ret < 0)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0;
            }
            DUALLOG_DEBUG("send failed: %s", strerror(errno));
            return -1;
        }
        return ret;
    }

    // 以非阻塞方式发送数据。
    ssize_t NonBlockSend(const void *buf, size_t len)
    {
        if (len == 0)
        {
            return 0;
        }
        return Send(buf, len, MSG_DONTWAIT);
    }
    // 关闭当前文件描述符，并将其重置为无效状态。
    void Close()
    {
        if (_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
    }
    // 创建服务端套接字；block_flag 为 true 时实际开启非阻塞模式。
    bool CreateServer(uint16_t port, const std::string &ip = "0.0.0.0", bool block_flag = false)
    {
        // 地址复用必须在绑定前设置，才能在旧连接处于 TIME_WAIT 时重新监听。
        if (!Create())
        {
            return false;
        }
        ReuseAddress();
        if (block_flag)
            NonBlock();
        if (!Bind(ip, port))
        {
            return false;
        }
        if (!Listen())
        {
            return false;
        }
        return true;
    }
    // 一次完成客户端套接字的创建以及到服务器的连接。
    bool CreateClient(uint16_t port, const std::string &ip)
    {
        // 1.创建套接字 2.连接服务器
        if (!Create())
        {
            return false;
        }
        if (!Connect(ip, port))
        {
            return false;
        }
        return true;
    }
    // 开启地址和端口复用选项。
    void ReuseAddress()
    {
        int opt = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        opt = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    }
    // 在文件状态标志中加入 O_NONBLOCK，将套接字设置为非阻塞模式。
    void NonBlock()
    {
        int flags = fcntl(_sockfd, F_GETFL, 0);
        fcntl(_sockfd, F_SETFL, flags | O_NONBLOCK);
    }
};
// 前置声明：Channel 通过 Eventloop 间接更新 Poller 中的事件。
class Poller;
class Eventloop;

// 文件描述符的事件代理。
// Channel 不拥有 fd，只记录“关注哪些事件”和“事件发生后调用谁”，
// 实际的 epoll 注册与删除通过所属 Eventloop 转交给 Poller 完成。
class Channel
{
private:
    int _fd;                                     // 当前 Channel 对应的文件描述符
    Eventloop *_loop;                            // 所属 Eventloop，用于更新/移除 epoll 监听
    uint32_t _events;                            // 当前需要监控的事件
    uint32_t _revents;                           // 当前连接触发的事件
    using EventCallback = std::function<void()>; // 统一的无参数事件回调类型
    EventCallback _read_callback;                // 可读事件被触发的回调函数
    EventCallback _write_callback;               // 可写事件被触发的回调函数
    EventCallback _error_callback;               // 错误事件被触发的回调函数
    EventCallback _close_callback;               // 连接断开事件被触发的回调函数
    EventCallback _event_callback;               // 任意事件被触发的回调函数

public:
    // 为 fd 创建事件代理，并记录其所属的 Eventloop；构造时尚未注册事件。
    Channel(Eventloop *loop, int fd) : _fd(fd), _events(0), _revents(0), _loop(loop) {}

    // 获取对应的文件描述符。
    int Fd() { return _fd; }

    // 设置 epoll 实际返回的就绪事件。
    void SetREvents(uint32_t events) { _revents = events; }

    // 获取实际就绪的事件。
    uint32_t Revents() { return _revents; }

    // 获取当前关注的事件。
    uint32_t Events() { return _events; }

    // 设置可读事件回调。
    void SetReadCallback(const EventCallback &cb) { _read_callback = cb; }

    // 设置可写事件回调。
    void SetWriteCallback(const EventCallback &cb) { _write_callback = cb; }

    // 设置错误事件回调。
    void SetErrorCallback(const EventCallback &cb) { _error_callback = cb; }

    // 设置连接关闭事件回调。
    void SetCloseCallback(const EventCallback &cb) { _close_callback = cb; }

    // 设置一次就绪事件处理完成后统一调用的回调。
    void SetEventCallback(const EventCallback &cb) { _event_callback = cb; }

    // 判断当前是否已经关注可读事件。
    bool ReadAble()
    {
        return (_events & EPOLLIN);
    }
    // 判断当前是否已经关注可写事件。
    bool WriteAble()
    {
        return (_events & EPOLLOUT);
    }
    // 开启可读事件监控，并同步到 Poller。
    void EnableRead()
    {
        _events |= EPOLLIN;
        Update();
    }
    // 开启可写事件监控，并同步到 Poller。
    void EnableWrite()
    {
        _events |= EPOLLOUT;
        Update();
    }
    // 关闭可读事件监控，并同步到 Poller。
    void DisableRead()
    {
        _events &= ~EPOLLIN;
        Update();
    }
    // 关闭可写事件监控，并同步到 Poller。
    void DisableWrite()
    {
        _events &= ~EPOLLOUT;
        Update();
    }
    // 关闭全部事件监控，并同步到 Poller。
    void DisableAll()
    {
        _events = 0;
        Update();
    }
    // 从 Poller 中移除当前 Channel。
    void Remove();

    // 将当前关注的事件同步到 Poller。
    void Update();

    // 根据 Poller 写入的 _revents 分发读、写、错误和关闭回调，
    // 最后调用一次通用事件回调。
    void HandleEvent()
    {
        // 读事件单独处理；对端半关闭时也先尝试读取尚未消费的数据。
        if ((_revents & EPOLLIN) || (_revents & EPOLLPRI) || (_revents & EPOLLRDHUP))
        {
            if (_read_callback)
            {
                _read_callback();
            }
        }

        // 当前分支按写、错误、挂断的优先级处理，三者在本轮最多执行一个。
        if (_revents & EPOLLOUT)
        {
            if (_write_callback)
            {
                _write_callback();
            }
        }
        else if (_revents & EPOLLERR)
        {
            if (_error_callback)
            {
                _error_callback();
            }
        }
        else if (_revents & EPOLLHUP)
        {
            if (_close_callback)
            {
                _close_callback();
            }
        }

        if (_event_callback)
        {
            _event_callback();
        }
    }
};

// epoll_wait() 单次最多返回的就绪事件数。
#define MAX_EVENTS 1024

// epoll 管理器，负责维护 fd 与 Channel 的映射、注册关注事件并等待事件就绪。
// Poller 没有内部锁，应当只由所属 Eventloop 线程访问。
// Channel 未设置 EPOLLET，当前使用水平触发；未读完的数据会再次触发可读事件。
class Poller
{
private:
    int _epollfd;                                 // 内核 epoll 实例对应的文件描述符
    struct epoll_event _events[MAX_EVENTS];       // epoll_wait() 的就绪事件输出数组
    std::unordered_map<int, Channel *> _channels; // 已注册 fd 到 Channel 的映射，不拥有 Channel
private:
    // 按照 op 调用 epoll_ctl()，添加、修改或删除一个 Channel 的关注事件。
    void UpDate(Channel *channel, int op)
    {
        int fd = channel->Fd();
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = channel->Events();
        if (epoll_ctl(_epollfd, op, fd, &ev) == -1)
        {
            DUALLOG_ERROR("epoll_ctl failed: %s", strerror(errno));
        }
    }

    // 判断 Channel 对应的 fd 是否已经登记在当前 Poller 中。
    bool HasChannel(Channel *channel)
    {
        return _channels.find(channel->Fd()) != _channels.end();
    }

public:
    // 创建 epoll 实例。
    Poller() : _epollfd(-1)
    {
        _epollfd = epoll_create1(EPOLL_CLOEXEC);
        if (_epollfd == -1)
        {
            DUALLOG_ERROR("epoll_create1 failed: %s", strerror(errno));
        }
    }

    // 同步 Channel 的关注事件：新 Channel 执行 ADD，已有 Channel 执行 MOD。
    void UpdateEvent(Channel *channel)
    {
        bool ret = HasChannel(channel);
        if (ret)
        {
            UpDate(channel, EPOLL_CTL_MOD);
        }
        else
        {
            UpDate(channel, EPOLL_CTL_ADD);
            _channels.insert(std::make_pair(channel->Fd(), channel));
        }
    }

    // 从内核 epoll 实例和本地映射表中移除 Channel。
    void RemoveEvent(Channel *channel)
    {
        if (HasChannel(channel))
        {
            UpDate(channel, EPOLL_CTL_DEL);
            _channels.erase(channel->Fd());
        }
    }

    // 阻塞等待事件，并把本轮活跃 Channel 写入 active。
    // 这里只收集事件，不执行回调；回调统一由 Eventloop 分发。
    void Poll(std::vector<Channel *> *active)
    {
        int nfds = epoll_wait(_epollfd, _events, MAX_EVENTS, -1);
        if (nfds == -1)
        {
            if (errno == EINTR)
            {
                return;
            }
            DUALLOG_ERROR("epoll_wait failed: %s", strerror(errno));
        }
        for (int i = 0; i < nfds; ++i)
        {
            int fd = _events[i].data.fd;
            uint32_t revents = _events[i].events;
            auto it = _channels.find(fd);
            assert(it != _channels.end());
            if (it != _channels.end())
            {
                Channel *channel = it->second;
                channel->SetREvents(revents);
                active->push_back(channel);
            }
        }
    }
};

// 定时任务结束后清理 TimerWheel 索引的回调类型。
using ReleaseFunc = std::function<void()>;
// 定时器到期时执行的业务回调类型。
using TaskFunc = std::function<void()>;

// 单个定时任务。
// 时间轮槽通过 shared_ptr 保存它；最后一个 shared_ptr 被释放时对象析构，
// 未取消的任务在析构函数中执行到期回调，并通过 _release 清理索引。
class TimeTask
{
private:
    uint64_t _id;         // 定时任务的唯一 ID
    uint32_t _timeout;    // 相对超时时间，也是刷新任务时使用的延迟
    bool _canceled;       // true 表示析构时不再执行任务回调
    TaskFunc _task_cb;    // 到期后执行的任务
    ReleaseFunc _release; // 对象析构后从 TimerWheel 索引中移除记录
public:
    // 创建一个尚未放入时间轮的定时任务。
    TimeTask(uint64_t id, uint32_t timeout, TaskFunc task)
        : _id(id),
          _timeout(timeout),
          _task_cb(task),
          _canceled(false)
    {
    }

    // 对象销毁代表时间轮不再持有该任务；未取消时执行任务回调。
    ~TimeTask()
    {
        if (!_canceled)
            _task_cb(); // 执行定时任务
        _release();     // 从 TimerWheel 中清除任务记录
    }
    // 标记任务为已取消，任务释放时只清理记录，不执行到期回调。
    void Cancel()
    {
        _canceled = true;
    }

    // 设置对象释放后的索引清理函数。
    void SetRelease(const ReleaseFunc &cb)
    {
        _release = cb;
    }

    // 获取创建时设置的延迟，用于重新计算刷新后的槽位。
    uint32_t DelayTime()
    {
        return _timeout;
    }
};
// 以一秒为一个刻度的单层时间轮。
// _wheel 中的 shared_ptr 控制任务生命周期，_tick 每秒向前移动一个槽；
// 添加、刷新和取消操作统一投递到所属 Eventloop，以保持线程安全。
// 当前有 60 个槽，常规延迟范围为 1～60 秒；没有跨轮计数，超出范围会取模。
class TimerWheel
{
private:
    using WeakTask = std::weak_ptr<TimeTask>;
    using PtrTask = std::shared_ptr<TimeTask>;

    int _capacity;                            // 时间轮槽数量，即当前实现可表达的周期长度
    std::vector<std::vector<PtrTask>> _wheel; // 槽数组，每个槽可以保存多个定时任务
    int _tick;                                // 秒针当前所在的槽下标

    // ID 到任务的弱引用索引；weak_ptr 不延长任务生命周期。
    std::unordered_map<uint64_t, WeakTask> _timers;

    Eventloop *_loop;                        // 所属事件循环，不拥有 Eventloop
    int _timerfd;                            // 每秒产生一次可读事件的 timerfd
    std::unique_ptr<Channel> _timer_channel; // 负责监听 _timerfd 的 Channel

private:
    // 定时任务结束后，从 ID 索引中删除记录。
    void RemoveTimer(uint64_t id)
    {
        auto it = _timers.find(id);

        if (it != _timers.end())
        {
            _timers.erase(it);
        }
    }
    // 创建并启动一个每秒触发一次的 timerfd。
    static int CreateTimerFd()
    {
        int timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (timerfd == -1)
        {
            DUALLOG_ERROR("timerfd_create failed: %s", strerror(errno));
        }
        struct itimerspec itime;
        itime.it_interval.tv_sec = 1; // 每秒触发一次
        itime.it_interval.tv_nsec = 0;
        itime.it_value.tv_sec = 1; // 初始延迟1秒
        itime.it_value.tv_nsec = 0;
        timerfd_settime(timerfd, 0, &itime, nullptr);
        return timerfd;
    }
    // 读取 timerfd 的到期计数，同时清除其可读状态。
    int ReadTimeFd()
    {
        uint64_t times;
        // 其他回调耗时较长时，timerfd 会累计多次到期；times 是上次读取后的到期次数。
        int ret = read(_timerfd, &times, 8);
        if (ret < 0)
        {
            // ERR_LOG("READ TIMEFD FAILED!");
            abort();
        }
        return times;
    }
    // 将时间轮向前推动一个槽，并释放该槽持有的定时任务。
    void RuntimerTask()
    {
        // 时间轮向前移动，走到末尾后回到0
        _tick = (_tick + 1) % _capacity;
        /*
            清空当前槽，释放其中的 shared_ptr。

            没刷新过：
                引用计数归0 -> TimeTask析构 -> 执行任务

            刷新过：
                新槽还有 shared_ptr -> 不析构
        */
        _wheel[_tick].clear();
    }
    // timerfd 的可读回调：补走所有累计刻度，避免事件循环忙碌时漏掉超时任务。
    void OnTime()
    {
        // 根据实际超时的次数，执行对应的超时任务
        int times = ReadTimeFd();
        for (int i = 0; i < times; i++)
        {
            RuntimerTask();
        }
    }
    // 在 Eventloop 线程中创建任务，并放入 delay 对应的槽位。
    void TimerAddInLoop(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        // 创建任务，由 shared_ptr 管理生命周期
        PtrTask pt(new TimeTask(id, delay, cb));

        // 任务析构后自动从 _timers 中删除
        pt->SetRelease(
            std::bind(&TimerWheel::RemoveTimer, this, id));

        // 根据当前位置和延迟时间计算任务所在槽
        int pos = (_tick + delay) % _capacity;

        // 时间轮中的 shared_ptr 真正控制任务生命周期
        _wheel[pos].push_back(pt);

        // 建立 ID -> 任务 的映射
        // weak_ptr 不增加引用计数
        _timers[id] = WeakTask(pt);
    }
    // 在 Eventloop 线程中刷新任务：从当前 tick 重新计算到期槽位。
    void TimerRefreshInLoop(uint64_t id)
    {
        auto it = _timers.find(id);

        if (it != _timers.end())
        {
            // weak_ptr 转换为 shared_ptr
            PtrTask pt = it->second.lock();

            if (!pt)
                return;

            // 获取原来的超时时间
            int delay = pt->DelayTime();

            // 从当前 tick 开始重新计算槽位置
            int pos = (_tick + delay) % _capacity;

            /*
                将同一个 shared_ptr 再放入新的槽。

                旧槽到期：
                    引用计数减1，但不会归0，因此任务不会执行。

                新槽到期：
                    最后一个 shared_ptr 被释放，
                    TimeTask 析构，真正执行任务。
            */
            _wheel[pos].push_back(pt);
        }
    }
    // 在 Eventloop 线程中取消任务，使其释放时不再执行到期回调。
    void TimerCancelInLoop(uint64_t id)
    {
        auto it = _timers.find(id);

        if (it != _timers.end())
        {
            // weak_ptr 转换为 shared_ptr
            PtrTask pt = it->second.lock();

            if (!pt)
                return;

            // 只设置取消标志，槽中的引用仍保留到该槽被清空时再释放。
            pt->Cancel();
        }
    }

public:
    // 创建 60 个槽的时间轮，并把 timerfd 注册到所属 Eventloop。
    TimerWheel(Eventloop *loop)
        : _capacity(60),
          _tick(0),
          _wheel(_capacity),
          _loop(loop),
          _timerfd(CreateTimerFd()),
          _timer_channel(new Channel(_loop, _timerfd))
    {
        _timer_channel->SetReadCallback(std::bind(&TimerWheel::OnTime, this));
        _timer_channel->EnableRead(); // 启动读时间监控
    }

    // 线程安全地添加定时任务；实际修改会投递到所属 Eventloop 执行。
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb);

    // 线程安全地刷新指定任务，从当前时刻重新计算延迟。
    void TimerRefresh(uint64_t id);

    // 线程安全地取消指定任务。
    void TimerCancel(uint64_t id);

    // 查询指定任务是否仍在索引中。
    // 该函数不加锁，只能在所属 Eventloop 线程中调用。
    bool HasTimer(uint64_t id)
    {
        auto it = _timers.find(id);
        if (it != _timers.end())
        {

            return true;
        }
        return false;
    }
};

// Reactor 事件循环。
// 一个 Eventloop 固定属于创建它的线程，负责等待 I/O、分发 Channel 回调、
// 执行跨线程投递的任务，并驱动 TimerWheel。
class Eventloop
{
private:
    std::thread::id _thread_id;              // 创建并拥有当前 Eventloop 的线程 ID
    int _event_fd;                           // 用于唤醒阻塞中的 epoll_wait()
    std::unique_ptr<Channel> _event_channel; // 监听 _event_fd 可读事件的 Channel
    Poller _poller;                          // 当前循环唯一的 epoll 管理器
    using Functor = std::function<void()>;   // 投递到循环线程执行的无参数任务
    std::vector<Functor> _task;              // 其他线程投递、等待在循环线程执行的任务队列
    std::mutex _mutex;                       // 保护 _task 的并发访问
    TimerWheel _timer_wheel;                 // 由当前循环驱动的定时器时间轮
public:
    // 取出任务队列的当前快照并逐个执行，执行回调时不持有互斥锁。
    void RunAllTask()
    {
        std::vector<Functor> tasks;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            // 快速交换队列后释放锁，任务执行期间其他线程仍可继续投递。
            tasks.swap(_task);
        }
        for (const auto &task : tasks)
        {
            task();
        }
    }
    // 创建用于跨线程唤醒 Eventloop 的非阻塞 eventfd。
    static int CreateEventFd()
    {
        int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (fd == -1)
        {
            DUALLOG_ERROR("eventfd failed: %s", strerror(errno));
        }
        return fd;
    }
    // 读取 eventfd 的累计计数，清除本次可读事件。
    void ReadEventfd()
    {
        uint64_t res = 0;
        int ret = read(_event_fd, &res, sizeof(res));
        if (ret <= 0)
        {
            // EINTR表示中断，EAGAIN表示资源暂时不可用，这两种情况不算错误，直接返回即可。
            if (errno == EINTR || errno == EAGAIN)
            {
                return;
            }
            DUALLOG_ERROR("read eventfd failed: %s", strerror(errno));
        }
        return;
    }
    // 向 eventfd 写入计数，使阻塞在 epoll_wait() 中的循环线程被唤醒。
    void WeakUpEventFd()
    {
        uint64_t res = 1;
        int ret = write(_event_fd, &res, sizeof(res));
        if (ret <= 0)
        {
            // EINTR表示中断，EAGAIN表示资源暂时不可用，这两种情况不算错误，直接返回即可。
            if (errno == EINTR || errno == EAGAIN)
            {
                return;
            }
            DUALLOG_ERROR("write eventfd failed: %s", strerror(errno));
        }
        return;
    }

public:
    // 在当前线程创建事件循环，并注册 eventfd 与 timerfd 的读事件。
    Eventloop() : _thread_id(std::this_thread::get_id()),
                  _event_fd(CreateEventFd()),
                  _timer_wheel(this),
                  _event_channel(new Channel(this, _event_fd))
    {
        _event_channel->SetReadCallback(std::bind(&Eventloop::ReadEventfd, this));
        _event_channel->EnableRead();
    }
    // 判断调用者是否正处于当前 Eventloop 所属线程。
    bool IsInLoop()
    {
        return _thread_id == std::this_thread::get_id();
    }
    // 断言当前函数由 Eventloop 所属线程调用。
    void AssertInLoop()
    {
        assert(IsInLoop());
    }
    // 在循环线程中执行任务：同线程立即执行，其他线程则投递到任务队列。
    void RunInloop(const Functor &cb)
    {
        if (IsInLoop())
        {
            return cb();
        }
        return QueueInLoop(cb);
    }
    // 将任务加入线程安全队列，并通过 eventfd 唤醒事件循环。
    void QueueInLoop(const Functor &cb)
    {
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _task.push_back(cb);
        }
        // 先入队再唤醒，循环线程被唤醒时才能看到本次任务。
        WeakUpEventFd();
    }
    // 将 Channel 当前关注的事件同步到 Poller。
    void UpdateEvent(Channel *channel)
    {
        return _poller.UpdateEvent(channel);
    }
    // 从 Poller 中移除 Channel 的事件监控。
    void RemoveEvent(Channel *channel)
    {
        return _poller.RemoveEvent(channel);
    }
    // 持续运行 Reactor：每轮等待事件、分发就绪回调，再执行任务队列。
    void Start()
    {
        while (true)
        {
            std::vector<Channel *> actives;
            _poller.Poll(&actives);
            for (auto &channel : actives)
            {
                channel->HandleEvent();
            }
            RunAllTask();
        }
    }
    // 向当前 Eventloop 的时间轮添加任务。
    void TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
    {
        _timer_wheel.TimerAdd(id, delay, cb);
    }
    // 刷新当前 Eventloop 中的指定定时任务。
    void TimerRefresh(uint64_t id)
    {
        _timer_wheel.TimerRefresh(id);
    }
    // 取消当前 Eventloop 中的指定定时任务。
    void TimerCancel(uint64_t id)
    {
        _timer_wheel.TimerCancel(id);
    }
    // 查询当前 Eventloop 的时间轮中是否存在指定任务。
    bool HasTimer(uint64_t id)
    {
        return _timer_wheel.HasTimer(id);
    }
};

// 将一个工作线程与其专属 Eventloop 绑定，实现 One Thread One Loop。
// Eventloop 在工作线程入口中创建，Getloop() 等待初始化完成后才返回地址。
class LoopThread
{
private:
    /*用于实现_loop获取的同步关系，避免线程创建了，但是_loop还没有实例化之前去获取_loop*/
    std::mutex _mutex;             // 保护 _loop 的发布与读取
    std::condition_variable _cond; // 通知等待者：工作线程中的 Eventloop 已创建
    Eventloop *_loop;              // 指向 ThreadEntry() 的局部对象，不拥有该对象
    std::thread _thread;           // 驱动该 Eventloop 的工作线程
private:
    // 实例化Eventloop对象,唤醒_cond上可能阻塞的线程，并且开始运行Eventloop模块的功能
    void ThreadEntry()
    {

        // 在工作线程中构造，确保 Eventloop 记录的线程 ID 与执行线程一致。
        Eventloop loop;
        {
            std::unique_lock<std::mutex> lock(_mutex);
            _loop = &loop;
        }
        _cond.notify_all(); // 通知主线程
        loop.Start();
    }

public:
    // 创建线程，设定线程入口函数
    LoopThread() : _loop(nullptr), _thread(std::thread(&LoopThread::ThreadEntry, this))
    {
    }
    // 等待初始化后返回工作线程的 Eventloop；条件谓词同时处理提前通知和虚假唤醒。
    Eventloop *Getloop()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock, [this]()
                   { return _loop != nullptr; });
        return _loop;
    }
};
// 管理多个 LoopThread，并以轮询方式为新连接分配工作 Eventloop。
// 分配由主循环完成；没有工作线程时退回主 Eventloop。
class LoopThreadPool
{
private:
    int _thread_count;                  // 从属线程的数量
    int _next_loop_idx;                 // 轮询分配时使用的工作循环下标
    Eventloop *_baseloop;               // 无工作线程时负责全部连接 I/O 的主循环
    std::vector<LoopThread *> _threads; // 保存所有的LoopThread对象
    std::vector<Eventloop *> _loops;    // 从属线程数量大于0则从_loops中进行线程EventLoop的分配
public:
    // 记录主循环，初始不创建工作线程。
    LoopThreadPool(Eventloop *baseloop) : _thread_count(0),
                                          _next_loop_idx(0),
                                          _baseloop(baseloop)
    {
    }
    // 配置工作线程数量，应在 Create() 启动线程前设置。
    void SetThreadCount(int count)
    {
        _thread_count = count;
    }
    // 创建所有的从属线程
    void Create()
    {
        if (_thread_count > 0)
        {
            _threads.resize(_thread_count);
            _loops.resize(_thread_count);
            for (int i = 0; i < _thread_count; i++)
            {
                _threads[i] = new LoopThread();
                _loops[i] = _threads[i]->Getloop();
            }
        }
    }
    // 获取本次连接应归属的 Eventloop；工作循环存在时轮询分配。
    Eventloop *Nextloop()
    {
        if (_thread_count == 0 || _threads.size() == 0)
        {
            return _baseloop;
        }
        _next_loop_idx = (_next_loop_idx + 1) % _thread_count;
        return _loops[_next_loop_idx];
    }
};

// TCP 连接在框架中的生命周期状态。
typedef enum ConStatu
{
    DISCONNECTED, // 连接断开
    CONNECTING,   // 连接建立中
    CONNECTED,    // 连接已建立
    DISCONNECTING // 连接断开中
} ConStatu;

// 简化版类型擦除容器，用于保存与具体协议有关的连接上下文。
// holder 提供统一接口，placeholder<T> 保存实际类型和值。
class Any
{
private:
    // 所有具体数据包装类型的抽象基类。
    class holder
    {
    public:
        // 通过基类指针释放实际包装对象时，同时析构其中保存的数据。
        virtual ~holder() {}
        // 返回被包装数据的实际类型，供 get<T>() 校验。
        virtual const std::type_info &type() = 0;
        // 复制一个独立的包装对象，避免两个 Any 共同持有同一裸指针。
        virtual holder *clone() = 0;
    };

    // 保存 T 类型数据的具体包装类。
    template <class T>
    class placeholder : public holder
    {
    public:
        // 复制并保存传入的数据。
        placeholder(const T &val) : _val(val) {}

        // 获取当前包装对象所保存的数据类型。
        virtual const std::type_info &type() { return typeid(T); }

        // 克隆当前包装对象，用于 Any 的深拷贝。
        virtual holder *clone() { return new placeholder(_val); }

    public:
        T _val; // 被类型擦除后实际保存的数据
    };
    holder *_content; // 指向实际数据包装对象；空 Any 中为 NULL

public:
    // 创建一个不保存数据的空容器。
    Any() : _content(NULL) {}

    // 创建容器并保存一份 val 的副本。
    template <class T>
    Any(const T &val) : _content(new placeholder<T>(val)) {}

    // 通过 clone() 深拷贝另一个容器。
    Any(const Any &other) : _content(other._content ? other._content->clone() : NULL) {}

    // 释放当前保存的数据包装对象。
    ~Any() { delete _content; }

    // 交换两个容器持有的数据，用于实现异常安全的赋值。
    Any &swap(Any &other)
    {
        std::swap(_content, other._content);
        return *this;
    }

    // 按指定类型获取数据地址；必须先保存数据，且请求类型必须与保存类型一致。
    template <class T>
    T *get()
    {
        // 想要获取的数据类型，必须和保存的数据类型一致
        assert(typeid(T) == _content->type());
        return &((placeholder<T> *)_content)->_val;
    }
    // 保存一份 val 的副本，并释放此前保存的数据。
    template <class T>
    Any &operator=(const T &val)
    {
        // 为val构造一个临时的通用容器，然后与当前容器自身进行指针交换，临时对象释放的时候，原先保存的数据也就被释放
        Any(val).swap(*this);
        return *this;
    }
    // 深拷贝另一个 Any，并替换当前内容。
    Any &operator=(const Any &other)
    {
        Any(other).swap(*this);
        return *this;
    }
};

class Connection;
// 连接表和异步任务共同持有连接，最后一个引用释放后才析构对象。
using PtrConnection = std::shared_ptr<Connection>;

// 一个已接收 TCP 连接的生命周期管理对象。
// Connection 把 Socket、Channel、收发缓冲区和业务回调组合在一起；
// 所有会改变连接状态的操作最终都应在所属 Eventloop 线程中执行。
// 继承 enable_shared_from_this 是为了在异步回调期间保持对象存活。
class Connection : public std::enable_shared_from_this<Connection>
{
private:
    uint64_t _id;                  // 连接唯一 ID，同时作为非活跃定时任务 ID
    int _sockfd;                   // 连接对应的文件描述符
    bool _enable_inactive_release; // 是否启用长时间无事件时自动释放连接
    Eventloop *_loop;              // 连接所属的事件循环，用于事件监控和回调处理
    ConStatu _statu;               // 连接状态，表示连接的生命周期阶段
    Socket _socket;                // 管理连接 fd 并提供收发接口
    Channel _channel;              // 将连接 fd 的就绪事件交给当前对象处理
    Buffer _in_buffer;             // 保存从 socket 收到、尚未被业务层消费的数据
    Buffer _out_buffer;            // 保存等待写入 socket 的数据
    Any _context;                  // 与具体协议或业务有关的连接上下文

    // 由上层服务器或业务模块设置的连接回调类型。
    using ConnectionCallback = std::function<void(const PtrConnection &)>;
    using MessageCallback = std::function<void(const PtrConnection &, Buffer *)>;
    using CloseCallback = std::function<void(const PtrConnection &)>;
    using AnyEventCallback = std::function<void(const PtrConnection &)>;

    MessageCallback _message_callback;       // 输入缓冲区出现数据时调用
    ConnectionCallback _connection_callback; // 连接完成初始化后调用
    CloseCallback _close_callback;           // 连接关闭时通知业务层
    AnyEventCallback _event_callback;        // 每次 Channel 事件处理后调用
    CloseCallback _server_closed_callback;   // 通知服务器从连接表中移除当前对象
private:
    // Channel 可读回调：读取 socket 数据，写入输入缓冲区并通知业务层。
    void HandleRead()
    {
        // 1.从socket接收数据存入输入缓冲区
        char buf[65536];
        ssize_t n = _socket.NonBlockRecv(buf, sizeof(buf));
        if (n < 0)
        {
            ShutdownInLoop();
             return;
        }
        _in_buffer.WriteAndPush(buf, n);
        if (_in_buffer.ReadAbleSize() > 0)
        {
            _message_callback(shared_from_this(), &_in_buffer);
            return;
        }
    }
    // Channel 可写回调：尽可能发送输出缓冲区的数据，发送完毕后取消写监控。
    void HandleWrite()
    {
        ssize_t ret = _socket.NonBlockSend(_out_buffer.ReadPosition(), _out_buffer.ReadAbleSize());
        if (ret < 0)
        {
            if (_in_buffer.ReadAbleSize() > 0 && _message_callback)
                _message_callback(shared_from_this(), &_in_buffer);

            Release();
            return;
        }
        // 非阻塞发送可能只写出部分数据，只消费实际发送的字节，余下部分留待下次。
        _out_buffer.MoveReadOffset(ret);
        if (_out_buffer.ReadAbleSize() == 0)
        {
            // TCP 通常一直可写，没有待发数据时取消写监控，避免循环被反复唤醒。
            _channel.DisableWrite();
            if (_statu == DISCONNECTING)
            {
                Release();
                return;
            }
        }
    }
    // Channel 挂断回调：有剩余数据时交给业务层处理，否则直接释放连接。
    void HandleClose()
    {

        /*一旦连接挂断了，套接字就什么都干不了了，因此有数据待处理就处理一下，完毕关闭连接*/
        if (_in_buffer.ReadAbleSize() > 0)
        {
            _message_callback(shared_from_this(), &_in_buffer);
        }
        Release();
        return;
    }
    // 描述符触发出错事件
    void HandleError()
    {
        return HandleClose();
    }
    // 描述符触发任意事件: 1. 刷新连接的活跃度--延迟定时销毁任务；  2. 调用组件使用者的任意事件回调
    void HandleEvent()
    {
        if (_enable_inactive_release)
        {
            _loop->TimerRefresh(_id);
        }
        if (_event_callback)
        {
            _event_callback(shared_from_this());
        }
    }
    // 在 Eventloop 线程中完成连接初始化：更新状态、开启读监控并触发连接回调。
    void EstablishInLoop()
    {
        // 1.修改连接状态为已建立
        // 2.启动读事件监控
        // 3.触发连接建立回调函数
        assert(_statu == CONNECTING); // 当前连接状态必须是连接建立中
        _statu = CONNECTED;
        // 注册后，后续就绪事件由事件循环分发到 HandleRead()，再通知业务层。
        _channel.EnableRead();
        if (_connection_callback)
        {
            _connection_callback(shared_from_this());
        }
    }

    // 在 Eventloop 线程中真正释放连接：移除事件、关闭 fd、取消定时器并触发回调。
    void ReleaseInLoop()
    {
        if (_statu == DISCONNECTED) return;
        // 1.修改连接状态为断开，将其置为DISCONNECTED状态，表示连接断开
        _statu = DISCONNECTED;
        // 2.移除连接的事件监控
        _channel.Remove();
        // 3.关闭连接的套接字
        _socket.Close();
        // 4.如果当前定时器中有该连接的定时器任务，则取消该定时器任务
        if (_loop->HasTimer(_id))
        {
            _loop->TimerCancel(_id);
        }
        // 5.调用关闭回调函数，避免先移除服务器管理的连接信息导致Connection对象被释放，在处理会出错，因此先调用用户设置的关闭回调函数，再移除服务器管理的连接信息
        if (_close_callback)
        {
            _close_callback(shared_from_this());
        }
        // 移除服务器内部管理的连接信息
        if (_server_closed_callback)
        {
            _server_closed_callback(shared_from_this());
        }
    }
    // 在 Eventloop 线程中把数据加入输出缓冲区并开启写监控；
    // 真正的 socket 写操作由 HandleWrite() 在 fd 可写时完成。
    void SendInLoop(Buffer &buf)
    {
        if (_statu == DISCONNECTED)
        {
            return;
        }

        _out_buffer.WriteBufferAndPush(buf);

        if (!_channel.WriteAble())
        {
            _channel.EnableWrite();
        }
    }
    // 在 Eventloop 线程中发起优雅关闭；有待发送数据时先等待发送完成。
    void ShutdownInLoop()
    {
        if (_statu == DISCONNECTED)
        {
            return;
        }

        _statu = DISCONNECTING;

        if (_out_buffer.ReadAbleSize() == 0)
        {
            Release();
            return;
        }

        // 还有数据就等待 HandleWrite() 发送完毕
        if (!_channel.WriteAble())
        {
            _channel.EnableWrite();
        }
    }
    // 在 Eventloop 线程中启用空闲连接释放；已有任务则刷新，否则创建任务。
    void EnableInactiveReleaseInLoop(int sec)
    {
        // 1.将判断标志 _enable_inactive_release 设置为true
        _enable_inactive_release = true;
        // 2.如果当前定时销毁任务已经存在，则刷新定时销毁任务的超时时间
        // 3.如果当前定时销毁任务不存在，则添加一个定时销毁任务
        if (_loop->HasTimer(_id))
        {
            _loop->TimerRefresh(_id);
        }
        else
        {
            _loop->TimerAdd(_id, sec, std::bind(&Connection::Release, shared_from_this()));
        }
    }
    // 在 Eventloop 线程中关闭空闲连接释放，并取消对应定时任务。
    void CancelInactiveReleaseInLoop()
    {
        _enable_inactive_release = false;
        if (_loop->HasTimer(_id))
        {
            _loop->TimerCancel(_id);
        }
    }
    // 在 Eventloop 线程中替换协议上下文和业务回调。
    void UpgradeInLoop(const Any &context, const ConnectionCallback &cb, const MessageCallback &msg_cb, const CloseCallback &close_cb, const AnyEventCallback &event_cb)
    {
        _context = context;
        _connection_callback = cb;
        _message_callback = msg_cb;
        _close_callback = close_cb;
        _event_callback = event_cb;
    }

public:
    // 创建处于 CONNECTING 状态的连接，并为 Channel 安装五类事件回调。
    Connection(Eventloop *loop, uint64_t id, int sockfd) : _id(id),
                                                           _sockfd(sockfd),
                                                           _enable_inactive_release(false),
                                                           _loop(loop),
                                                           _statu(CONNECTING),
                                                           _socket(sockfd),
                                                           _channel(loop, sockfd),
                                                           _in_buffer(),
                                                           _out_buffer(),
                                                           _context(),
                                                           _message_callback(nullptr),
                                                           _connection_callback(nullptr),
                                                           _close_callback(nullptr),
                                                           _event_callback(nullptr)
    {
        _channel.SetReadCallback(std::bind(&Connection::HandleRead, this)),
            _channel.SetWriteCallback(std::bind(&Connection::HandleWrite, this)),
            _channel.SetErrorCallback(std::bind(&Connection::HandleError, this)),
            _channel.SetCloseCallback(std::bind(&Connection::HandleClose, this)),
            _channel.SetEventCallback(std::bind(&Connection::HandleEvent, this));
    }
    // 记录连接对象析构；正常情况下底层 fd 由 ReleaseInLoop() 提前关闭。
    ~Connection()
    {
        //DUALLOG_INFO("RELEASE CONNECTION:%p", this);
    }
    // 获取当前连接管理的文件描述符。
    int Fd()
    {
        return _sockfd;
    }
    // 获取连接唯一 ID。
    int Id()
    {
        return _id;
    }
    // 判断连接是否处于可通信的 CONNECTED 状态。
    bool Connected()
    {
        return _statu == CONNECTED;
    }
    // 设置与当前连接绑定的协议或业务上下文。
    void SetContext(const Any &context)
    {
        _context = context;
    }
    // 获取连接上下文地址，允许业务层读取或修改其中的数据。
    Any *GetContext()
    {
        return &_context;
    }
    // 设置连接完成初始化后的回调。
    void SetConnectionCallback(const ConnectionCallback &cb)
    {
        _connection_callback = cb;
    }
    // 设置收到数据后的消息处理回调。
    void SetMessageCallback(const MessageCallback &cb)
    {
        _message_callback = cb;
    }
    // 设置连接关闭时通知业务层的回调。
    void SetCloseCallback(const CloseCallback &cb)
    {
        _close_callback = cb;
    }
    // 设置每次 Channel 事件处理完成后的通用回调。
    void SetAnyEventCallback(const AnyEventCallback &cb)
    {
        _event_callback = cb;
    }
    // 设置服务器内部关闭回调，用于从服务器连接表中移除当前连接。
    void SetServerClosedCallback(const CloseCallback &cb)
    {
        _server_closed_callback = cb;
    }

    // 线程安全地完成连接初始化；实际操作由所属 Eventloop 执行。
    void Establish()
    {
        _loop->RunInloop(std::bind(&Connection::EstablishInLoop, shared_from_this()));
    }
    // 复制待发送数据并投递到所属 Eventloop，避免异步执行时原始内存失效。
    void Send(const char *date, size_t len)
    {
        Buffer buf;
        buf.WriteAndPush(date, len);
        // 任务保存缓冲区和连接的共享引用，跨线程等待期间不会依赖调用者的内存。
        _loop->RunInloop(std::bind(&Connection::SendInLoop, shared_from_this(), std::move(buf)));
    }
    // 线程安全地发起优雅关闭；该接口不会立即丢弃尚未发送的数据。
    void Shutdown()
    {
        _loop->RunInloop(std::bind(&Connection::ShutdownInLoop, shared_from_this()));
    }
    void Release()
    {
        _loop->QueueInLoop(
            std::bind(&Connection::ReleaseInLoop, shared_from_this()));
    }
    // 启用非活跃连接释放，sec 表示允许的最大空闲秒数。
    void EnableInactiveRelease(int sec)
    {
        _loop->RunInloop(std::bind(&Connection::EnableInactiveReleaseInLoop, shared_from_this(), sec));
    }
    // 关闭非活跃连接释放并取消对应定时任务。
    void CancelInactiveRelease()
    {
        _loop->RunInloop(std::bind(&Connection::CancelInactiveReleaseInLoop, shared_from_this()));
    }
    // 切换连接协议：同时替换上下文和阶段性回调。
    // 必须在所属 Eventloop 线程中调用，避免新事件仍被旧协议处理。
    void Upgrade(const Any &context, const ConnectionCallback &cb, const MessageCallback &msg_cb, const CloseCallback &close_cb, const AnyEventCallback &event_cb)
    {
        _loop->AssertInLoop();
        _loop->RunInloop(std::bind(&Connection::UpgradeInLoop, shared_from_this(), context, cb, msg_cb, close_cb, event_cb));
    }
};

// 监听端口的事件入口；主循环收到可读事件时 accept()，再把新 fd 交给 TcpServer。
class Acceptor
{
private:
    Socket _socket;   // 用于创建监听套接字
    Eventloop *_loop; // 用于对监听套接字进行事件监控
    Channel _channel; // 用于对监听套接字进行事件管理

    using AcceptCallback = std::function<void(int)>; // 接收新连接 fd 的回调类型
    AcceptCallback _accept_callback;                 // 通知上层创建并管理 Connection

private:
    /*监听套接字的读事件回调处理函数---获取新连接，调用_accept_callback函数进行新连接处理*/
    void HandleRead()
    {
        int newfd = _socket.Accept();
        if (newfd < 0)
        {
            return;
        }
        if (_accept_callback)
            _accept_callback(newfd);
    }
    // 创建监听套接字并返回 fd，供 _channel 构造时绑定。
    int CreateServer(int port)
    {
        bool ret = _socket.CreateServer(port);
        assert(ret == true);
        return _socket.Fd();
    }

public:
    /*不能将启动读事件监控，放到构造函数中，必须在设置回调函数后，再去启动*/
    /*否则有可能造成启动监控后，立即有事件，处理的时候，回调函数还没设置：新连接得不到处理，且资源泄漏*/
    // 创建监听 socket 并安装内部读回调，事件注册由 Listen() 单独完成。
    Acceptor(Eventloop *loop, int port)
        : _socket(),
          _loop(loop),
          _channel(loop, CreateServer(port))
    {
        _channel.SetReadCallback(
            std::bind(&Acceptor::HandleRead, this));
    }
    // 设置新连接回调，应在 Listen() 开启事件监控前调用。
    void SetAcceptCallback(const AcceptCallback &cb) { _accept_callback = cb; }
    // socket 已在构造阶段进入监听状态；这里仅向主循环注册可读事件。
    void Listen() { _channel.EnableRead(); }
};

// TCP 服务的组装入口：主循环接收连接，线程池处理连接 I/O，回调衔接具体业务。
// _conns 只在主循环中增删，Connection 的状态和缓冲区由各自所属循环维护。
class TcpServer
{
private:
    uint64_t _next_id;             // 主循环分配的递增 ID，连接与 RunAfter() 任务共用
    int _port;                     // 服务监听端口
    int _timeout;                  // 非活跃连接的统计时间--多长时间无通信就是非活跃连接
    bool _enable_inactive_release; // 是否启动非活跃连接超时销毁的标志

    Eventloop _baseloop;                                // 这是主线程的EventLoop对象，负责监听事件的处理
    Acceptor _acceptor;                                 // 这是监听套接字的管理对象
    LoopThreadPool _pool;                               // 这个是从属Eventloop线程池
    std::unordered_map<uint64_t, PtrConnection> _conns; // 持有活动连接的共享引用，按 ID 索引

    // 由上层服务器或业务模块设置的连接回调类型。
    using ConnectionCallback = std::function<void(const PtrConnection &)>;
    using MessageCallback = std::function<void(const PtrConnection &, Buffer *)>;
    using CloseCallback = std::function<void(const PtrConnection &)>;
    using AnyEventCallback = std::function<void(const PtrConnection &)>;

    using Functor = std::function<void()>; // RunAfter() 接收的延迟任务类型

    MessageCallback _message_callback;       // 输入缓冲区出现数据时调用
    ConnectionCallback _connection_callback; // 连接完成初始化后调用
    CloseCallback _close_callback;           // 连接关闭时通知业务层
    AnyEventCallback _event_callback;        // 每次 Channel 事件处理后调用
    CloseCallback _server_closed_callback;   // 当前 TcpServer 中未使用；连接移除回调在 NewConnection() 中绑定
private:
    // 关闭通知可能来自工作线程，先投递到主循环，再修改连接表。
    void RemoveConnection(const PtrConnection &conn)
    {
        _baseloop.RunInloop(std::bind(&TcpServer::RemoveConnectionInLoop, this, conn));
    }
    // 在主循环中移除连接的服务器引用；其他异步任务仍可持有该连接。
    void RemoveConnectionInLoop(const PtrConnection &conn)
    {
        int id = conn->Id();
        auto it = _conns.find(id);
        if (it != _conns.end())
        {
            _conns.erase(it);
        }
    }
    // 为新连接构建一个Connection进行管理
    void NewConnection(int fd)
    {
        _next_id++;
        // 为连接选定唯一的工作循环，后续 I/O 和业务回调都由该循环执行。
        PtrConnection conn(new Connection(_pool.Nextloop(), _next_id, fd));
        conn->SetMessageCallback(_message_callback);
        conn->SetCloseCallback(_close_callback);
        conn->SetConnectionCallback(_connection_callback);
        conn->SetAnyEventCallback(_event_callback);
        // 业务关闭回调结束后，再通知主循环从连接表删除该连接。
        conn->SetServerClosedCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));
        if (_enable_inactive_release)
            conn->EnableInactiveRelease(_timeout); // 启动非活跃超时销毁
        conn->Establish();                         // 就绪初始化
        _conns.insert(std::make_pair(_next_id, conn));
    }

    // 在主循环中为一次性任务分配 ID，并放入主循环的时间轮。
    void RunAfterInLoop(const Functor &task, int delay)
    {
        _next_id++;
        _baseloop.TimerAdd(_next_id, delay, task);
    }

public:
    // 创建主循环、监听器和线程池，安装接收回调后才注册监听事件。
    TcpServer(uint16_t port) : _port(port),
                               _next_id(0),
                               _enable_inactive_release(false),
                               _acceptor(&_baseloop, port),
                               _pool(&_baseloop)
    {

        _acceptor.SetAcceptCallback(std::bind(&TcpServer::NewConnection, this, std::placeholders::_1));
        _acceptor.Listen(); // 将监听套接字挂到baseloop上
    }
    // 配置工作线程数量，应在 start() 启动服务前调用。
    void SetThreadCount(int count)
    {
        _pool.SetThreadCount(count);
    }
    // 设置连接完成初始化后的回调。
    void SetConnectionCallback(const ConnectionCallback &cb)
    {
        _connection_callback = cb;
    }
    // 设置收到数据后的消息处理回调。
    void SetMessageCallback(const MessageCallback &cb)
    {
        _message_callback = cb;
    }
    // 设置连接关闭时通知业务层的回调。
    void SetCloseCallback(const CloseCallback &cb)
    {
        _close_callback = cb;
    }
    // 设置每次 Channel 事件处理完成后的通用回调。
    void SetAnyEventCallback(const AnyEventCallback &cb)
    {
        _event_callback = cb;
    }
    // 为此后创建的连接启用空闲超时释放；timeout 的单位是秒。
    void EnableInactiveRelease(int timeout)
    {
        _timeout = timeout;
        _enable_inactive_release = true;
    }
    // 线程安全地添加一次性延迟任务，delay 为秒数，回调在主循环线程中执行。
    // 延迟范围受 TimerWheel 的单层时间轮容量限制。
    void RunAfter(const Functor &task, int delay)
    {
        _baseloop.RunInloop(std::bind(&TcpServer::RunAfterInLoop, this, task, delay));
    }
    // 创建工作循环并进入主循环；该函数持续运行，不主动返回。
    void start()
    {
        _pool.Create();
        _baseloop.Start();
    }
};
// 从所属 Poller 中移除当前 Channel。
void Channel::Remove()
{
    _loop->RemoveEvent(this);
}

// 将当前关注的事件更新到所属 Poller。
void Channel::Update()
{
    _loop->UpdateEvent(this);
}

// 将新增定时任务投递到所属 Eventloop，保证时间轮只在一个线程中修改。
void TimerWheel::TimerAdd(uint64_t id, uint32_t delay, const TaskFunc &cb)
{
    _loop->RunInloop(std::bind(&TimerWheel::TimerAddInLoop, this, id, delay, cb));
}

// 刷新定时任务：从当前时间重新计算超时时间
void TimerWheel::TimerRefresh(uint64_t id)
{
    _loop->RunInloop(std::bind(&TimerWheel::TimerRefreshInLoop, this, id));
}

// 将取消任务操作投递到所属 Eventloop。
void TimerWheel::TimerCancel(uint64_t id)
{
    _loop->RunInloop(std::bind(&TimerWheel::TimerCancelInLoop, this, id));
}

// 网络运行环境初始化：忽略 SIGPIPE，避免对端关闭后写 socket 导致进程退出。
class NetWork
{
public:
    // 在静态初始化期间设置进程级信号处理方式。
    NetWork()
    {
        signal(SIGPIPE, SIG_IGN);
    }
};

// 头文件内的静态对象使每个引用该头文件的翻译单元在进入 main() 前完成初始化。
static NetWork nw;

#endif
