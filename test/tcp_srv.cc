#include "../source/server.hpp"
Eventloop base_loop;
std::unordered_map<uint64_t, PtrConnection> _conns;
uint64_t id = 0;
LoopThreadPool *loop_pool;
void ConnectionDestroyed(const PtrConnection &conn)
{
    _conns.erase(conn->Id());
    DUALLOG_DEBUG("connection destroyed, id: %lu", conn->Id());
}
void OnConnection(const PtrConnection &conn)
{
    DUALLOG_DEBUG("NEW CONNECTION: %p", conn.get());
}
void OnMessage(const PtrConnection &conn, Buffer *buffer)
{
    DUALLOG_DEBUG("%s", buffer->ReadPosition());
    buffer->MoveReadOffset(buffer->ReadAbleSize());
    std::string str = "Hello, client!";
    conn->Send(str.c_str(), str.size());
}
void Newconnection(int fd)
{
    id++;
    PtrConnection conn = std::make_shared<Connection>(loop_pool->Nextloop(), id, fd);
    conn->SetMessageCallback(std::bind(OnMessage, std::placeholders::_1, std::placeholders::_2));
    conn->SetCloseCallback(std::bind(ConnectionDestroyed, std::placeholders::_1));
    conn->SetConnectionCallback(std::bind(OnConnection, conn));
    conn->EnableInactiveRelease(10);
    conn->Establish();
    DUALLOG_DEBUG("NEW------");
    _conns.insert(std::make_pair(id, conn));
}
int main()
{
    loop_pool = new LoopThreadPool(&base_loop);
    loop_pool->SetThreadCount(2);
    loop_pool->Create();
    Acceptor acceptor(&base_loop, 8080);
    acceptor.SetAcceptCallback(std::bind(Newconnection, std::placeholders::_1));
    acceptor.Listen();
    base_loop.Start();

    return 0;
}
