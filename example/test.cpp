#include <iostream>
#include <string>
#include <vector>
// 字符串分割函数,将src字符串按照sep字符串进行分割，得到的各个子串放到arry中，最终返回子串的数量
size_t Split(const std::string &src, const std::string &sep, std::vector<std::string> *arry)
{
    int offset = 0;
    while (offset < src.size())
    {
        size_t pos = src.find(sep, offset); // 在src字符串偏移量offset处,开始向后查找sep字符/子串,返回查到的位置
        if (pos == std::string::npos)       // 没有找到特定的字符
        {
            if (pos == src.size())
                break;
            // 将剩余的部分当做一个子串，放入arry中
            arry->push_back(src.substr(offset));
            return arry->size();
        }
        if (pos == offset)
        {
            offset = pos + sep.size();
            continue;
        }
        arry->push_back(src.substr(offset, pos - offset));
        offset = pos + sep.size();
    }

    return arry->size();
}

int main()
{
    std::string str = "abc,bcd,def,";
    std::vector<std::string> arry;
    Split(str, ",", &arry);
    for (auto &s : arry)
    {
        std::cout << "[" << s << "]\n";
    }
    return 0;
}