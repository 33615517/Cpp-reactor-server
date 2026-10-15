#include"../source/server.hpp"
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
    DUALLOG_DEBUG("%s", buffer->ReadPosition());
    buffer->MoveReadOffset(buffer->ReadAbleSize());
    std::string str = "Hello, client!";
    conn->Send(str.c_str(), str.size());
   //  conn->Shutdown();
}

int main()
{
    TcpServer server(8080);
    server.SetThreadCount(2);
    server.EnableInactiveRelease(10);
    server.SetCloseCallback(OnClosed);
    server.SetConnectionCallback(OnConnection);
    server.SetMessageCallback(OnMessage);
    server.start();
    return 0;
}