#ifndef DUALLOG_H
#define DUALLOG_H
#include "logger.hpp"
namespace duallog
{

    // 1. 提供获取指定日志器的全局接口（避免用户自己操作单例对象）
    inline Logger::ptr getLogger(const std::string &name)
    {
        return duallog::LoggerManager::getInstance().getLogger(name);
    }
    // 获取系统自动创建的默认 root 日志器。
    inline Logger::ptr rootLogger()
    {
        return duallog::LoggerManager::getInstance().rootLogger();
    }
// 2. 使用宏函数对日志器的接口进行代理（代理模式）
#define debug(fmt, ...) debug(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define info(fmt, ...) info(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define warn(fmt, ...) warn(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define error(fmt, ...) error(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define fatal(fmt, ...) fatal(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
// 3. 提供宏函数，直接通过默认日志器进行日志的标准输出打印（不用获取日志器了）
#define DUALLOG_DEBUG(fmt, ...) duallog::rootLogger()->debug(fmt, ##__VA_ARGS__)
#define DUALLOG_INFO(fmt, ...) duallog::rootLogger()->info(fmt, ##__VA_ARGS__)
#define DUALLOG_WARN(fmt, ...) duallog::rootLogger()->warn(fmt, ##__VA_ARGS__)
#define DUALLOG_ERROR(fmt, ...) duallog::rootLogger()->error(fmt, ##__VA_ARGS__)
#define DUALLOG_FATAL(fmt, ...) duallog::rootLogger()->fatal(fmt, ##__VA_ARGS__)
}
#endif
