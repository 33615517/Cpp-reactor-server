/*
    1.定义枚举类，枚举出日志等级
    2.提供转换接口：将枚举转换为对应字符串


*/
#ifndef DUALLOG_LEVEL_HPP
#define DUALLOG_LEVEL_HPP
namespace duallog
{
    // 日志等级工具类：定义日志等级，并提供等级到字符串的转换。
    class LogLevel
    {

     public:
        // 日志等级；数值越大表示日志越严重，OFF 表示关闭日志。
        enum class Level
        {
            UNKNOW=0,
            DEBUG,
            INFO,
            WARN,
            ERROR,
            FATAL,
            OFF
        };
        // 将日志等级转换为便于输出的字符串。
        static const char *toString(LogLevel::Level level)
        {
            switch (level)
            {
            case LogLevel::Level::DEBUG:
                return "DEBUG";
            case LogLevel::Level::INFO:
                return "INFO";
            case LogLevel::Level::WARN:
                return "WARN";
            case LogLevel::Level::ERROR:
                return "ERROR";
            case LogLevel::Level::FATAL:
                return "FATAL";
            case LogLevel::Level::OFF:
                return "OFF";
            default:
                return "UNKNOW";
            }
        }
    };
}
#endif
