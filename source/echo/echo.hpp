#include "../server.hpp"

// TCP 回显示例：将本次收到的数据原样发回，发送完后关闭连接。
// 借助 TcpServer 演示业务回调、工作线程和空闲连接释放的配置方式。
class EchoServer
{
private:
    TcpServer _server; // 提供监听、连接管理和 Reactor 事件分发的网络核心

private:
    // 新连接完成初始化时记录日志，回调运行在连接所属的 Eventloop 线程中。
    void OnConnection(const PtrConnection &conn)
    {
        DUALLOG_DEBUG("NEW CONNECTION: %p", conn.get());
    }
    // 底层连接关闭后记录日志，由 TcpServer 随后移除连接表中的引用。
    void OnClosed(const PtrConnection &conn)
    {
        DUALLOG_DEBUG("CLOSE CONNECTION: %p", conn.get());
    }
    // 处理当前输入缓冲区的全部数据；TCP 字节流不保证一次回调对应一次客户端发送。
    void OnMessage(const PtrConnection &conn, Buffer *buffer)
    {
        // Send() 先复制数据到输出缓冲区，实际发送由可写事件回调完成。
        conn->Send((const char*)buffer->ReadPosition(), buffer->ReadAbleSize());
        // 标记输入已消费，避免下一次消息回调再次处理同一批数据。
        buffer->MoveReadOffset(buffer->ReadAbleSize());
        // 进入关闭流程，等待输出缓冲区发送完毕后再释放连接。
        conn->Shutdown();
    }

public:
    // 监听指定端口，使用两个工作循环，并释放 10 秒内没有事件的连接。
    EchoServer(int port) : _server(port)
    {
        _server.SetThreadCount(2);
        _server.EnableInactiveRelease(10);
        // 将业务成员函数绑定为服务器回调；占位符接收连接和输入缓冲区参数。
        _server.SetCloseCallback(std::bind(&EchoServer::OnClosed, this, std::placeholders::_1));
        _server.SetConnectionCallback(std::bind(&EchoServer::OnConnection, this, std::placeholders::_1));
        _server.SetMessageCallback(std::bind(&EchoServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    }
    // 启动工作线程和主事件循环，持续接收并处理连接。
    void Start() { _server.start(); }
};
