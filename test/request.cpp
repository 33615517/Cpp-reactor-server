#include <iostream>
#include <regex>
#include <string>

int main()
{
    std::string str = "GET /xingguichutu/login?user=chen&pass=123 HTTP/1.1\r\n";
    std::smatch matches;

    std::regex e("(GET|HEAD|POST|PUT|DELETE) ([^?]*)(?:\\?(.*))? (HTTP/1\\.[01])(?:\n|\r\n)?");
    bool ret = std::regex_match(str, matches, e);
    if (ret == false)
    {
        return -1;
    }
    for (auto &s : matches)
    {
        std::cout << s << std::endl;
    }
    return 0;
}