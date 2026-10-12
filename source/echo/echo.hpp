#include "../server.hpp"

class EchoServer
{
private:
    TcpServer _server;

private:
    void OnConnection(const PtrConnection &conn)
    {
        DUALLOG_DEBUG("NEW CONNECTION: %p", conn.get());
    }
    void OnClosed(const PtrConnection &conn)
    {
        DUALLOG_DEBUG("CLOSE CONNECTION: %p", conn.get());
    }
    void OnMessage(const PtrConnection &conn, Buffer *buffer)
    {
        conn->Send((const char*)buffer->ReadPosition(), buffer->ReadAbleSize());
        buffer->MoveReadOffset(buffer->ReadAbleSize());
        conn->Shutdown();
    }

public:
    EchoServer(int port) : _server(port)
    {
        _server.SetThreadCount(2);
        _server.EnableInactiveRelease(10);
        _server.SetCloseCallback(std::bind(&EchoServer::OnClosed, this, std::placeholders::_1));
        _server.SetConnectionCallback(std::bind(&EchoServer::OnConnection, this, std::placeholders::_1));
        _server.SetMessageCallback(std::bind(&EchoServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    }
    void Start() { _server.start(); }
};