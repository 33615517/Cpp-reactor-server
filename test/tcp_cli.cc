#include "../source/server.hpp"

int main()
{
    Socket cli_sock;
    cli_sock.CreateClient(8080, "127.0.0.1");
    for(int i=1;i<=5;i++)
    {

        std::string str = "Xingguichutu";
        cli_sock.Send(str.c_str(), str.size(), 0);
        char buf[1024] = {0};
        cli_sock.Recv(buf, sizeof(buf), 0);
        DUALLOG_DEBUG("recv: %s", buf);
        sleep(1);
    }
    while(1)
    {
        sleep(1);
    }
    return 0;
}