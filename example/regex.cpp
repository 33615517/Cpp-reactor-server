#include<iostream>
#include<regex>
#include<string>

int main()
{
    std::string str="/numbers/1234";
    std::regex pattern("/numbers/(\\d+)");
    std::smatch match;

    bool ret = std::regex_search(str, match, pattern);
    if(!ret)
    {
        std::cout<<"匹配失败"<<std::endl;
        return 0;
    }
    for(auto& m: match)
    {
        std::cout<<m.str()<<std::endl;
    }
    return 0;
}