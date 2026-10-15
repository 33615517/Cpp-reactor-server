/*
    定义日志消息类，进行日志中间信息的存储：
    1. 日志的输出时间        用于过滤日志输出时间
    2. 日志等级            用于进行日志过滤分析
    3. 源文件名称
    4. 源代码行号          用于定位出现错误的代码位置
    5. 线程ID              用于过滤出错的线程
    6. 日志主体消息
    7. 日志器名称          （当前支持多日志器的同时使用）
*/
#ifndef DUALLOG_MESSAGE_HPP
#define DUALLOG_MESSAGE_HPP
#include "level.hpp"
#include <string>
#include <thread>
#include "util.hpp"
#include <iostream>
#include <cstddef>
namespace duallog
{
    // 一条日志的完整上下文，供格式化器读取和输出。
    struct LogMsg
    {
        time_t _ctime;          // 日志产生的时间戳
        LogLevel::Level _level; // 日志等级
        size_t _line;           // 行号
        std::thread::id _tid;   // 线程ID
        std::string _file;      // 源文件名称
        std::string _logger;    // 日志器名称
        std::string _payload;   // 日志主体消息
        // 创建日志消息，并自动记录当前时间和当前线程 ID。
        LogMsg(LogLevel::Level level,
               size_t line,
               const std::string &file,
               const std::string &logger,
               std::string msg)
            : _ctime(util::Date::now()),
              _level(level),
              _line(line),
              _tid(std::this_thread::get_id()),
              _file(file),
              _logger(logger),
              _payload(std::move(msg))
        {
        }
    };
}
#endif
