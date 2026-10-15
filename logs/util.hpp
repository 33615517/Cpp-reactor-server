#ifndef DUALLOG_UTIL_HPP
#define DUALLOG_UTIL_HPP

/*实用工具类的时间：
    1.获取系统时间
    2.判断文件是否存在
    3.获取文件所在路径
    4。创建目录
*/
#include <sys/stat.h>
#include <ctime>
#include <string>
#include <iostream>
#include <unistd.h>
namespace duallog
{
    namespace util
    {
        // 时间工具：提供日志系统需要的当前时间戳。
        class Date
        {
        public:
            // 获取当前 Unix 时间戳，单位为秒。
            static size_t now()
            {
                return (size_t)time(nullptr);
            }
        };
        // 文件工具：负责路径解析、存在性检查和递归创建目录。
        class File
        {
        public:
            // 判断指定文件或目录是否存在。
            static bool exists(const std::string &pathname)
            {
                // return access(pathname.c_str(), F_OK) == 0;
                struct stat st;
                if (stat(pathname.c_str(), &st) == 0)
                {
                    return true;
                }
                else
                {
                    return false;
                }
            }
            // 从完整路径中提取目录部分；没有目录时返回当前目录 "."。
            static std::string path(const std::string &pathname)
            {
                size_t pos = pathname.find_last_of("/\\");
                if (pos == std::string::npos)
                {
                    return ".";
                }
                else
                {
                    return pathname.substr(0, pos + 1);
                }
            }
            // 按路径层级创建目录，已经存在的目录会被跳过。
            static void createDirectory(const std::string &pathname)
            {
                size_t pos = 0, idx = 0; // pos 是分隔符位置，idx 是本轮查找起点。
                while (idx < pathname.size())
                {
                    pos = pathname.find_first_of("/\\", idx);
                    if (pos == std::string::npos)
                    {
                        mkdir(pathname.c_str(), 0777);
                        break;
                    }
                    std::string parent_dir = pathname.substr(0, pos + 1); // 当前需要检查的父目录。
                    // if(parent_dir == "." || parent_dir == "..")
                    // {
                    //     idx = pos + 1;
                    //     continue;
                    // }
                    if (exists(parent_dir) == true)
                    {
                        idx = pos + 1;
                        continue;
                    }
                    mkdir(parent_dir.c_str(), 0777);
                    idx = pos + 1;

                }
            }
        };
    }
}
#endif
