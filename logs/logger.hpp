/*完成日志器模块
    1.抽象日志基类
    2.派生出不同的子类（同步日志器&异步日志器）

*/
#ifndef DUALLOG_LOGGER_HPP
#define DUALLOG_LOGGER_HPP
#include "sink.hpp"
#include "level.hpp"
#include "format.hpp"
#include "util.hpp"
#include "looper.hpp"
#include <atomic>
#include <mutex>
#include <cstdarg>
#include <unordered_map>
namespace duallog
{
    // 日志器抽象基类：负责接收日志请求，并保存格式器和输出目标。
    class Logger
    {
    public:
        using ptr = std::shared_ptr<Logger>; // 日志器的共享指针类型。
        // 保存日志器名称、最低输出等级、格式器和所有落地目标。
        Logger(const std::string &logger_name, LogLevel::Level limit_level, Formatter::ptr formatter, const std::vector<LogSink::ptr> &sinks)
            : _logger_name(logger_name), _limit_level(limit_level), _formatter(formatter), _sinks(sinks) {
              };
        virtual ~Logger() = default;
        // 获取日志器名称。
        const std::string &name()
        {
            return _logger_name;
        }
        /*完成构造日志消息对象过程并进行格式化，得到格式化后的日志消息字符串---然后进行落地输出*/
        // 记录 DEBUG 级别日志。
        void debug(const char *file, size_t line, const char *fmt, ...)
        {
            // 通过传入的参数构造日志消息对象，进行日志的格式化，最终落地
            // 1.判断当前日志等级是否达到输出等级要求
            if (_limit_level.load() > LogLevel::Level::DEBUG)
            {
                return;
            }

            // 2.对fmt格式化字符串和不定参进行字符串组织，得到的日志消息的字符串
            //  处理日志消息
            va_list ap;
            va_start(ap, fmt);
            char *res = nullptr;
            int ret = vasprintf(&res, fmt, ap);
            va_end(ap);
            if (ret == -1)
            {
                std::cout << "vasprintf failed!\n,日志消息格式化失败" << std::endl;
                return;
            }
            serialize(LogLevel::Level::DEBUG, file, line, res);
            // 释放内存
            free(res);
        }
        // 记录 INFO 级别日志。
        void info(const char *file, size_t line, const char *fmt, ...)
        {
            // 通过传入的参数构造日志消息对象，进行日志的格式化，最终落地
            // 1.判断当前日志等级是否达到输出等级要求
            if (_limit_level.load() > LogLevel::Level::INFO)
            {
                return;
            }

            // 2.对fmt格式化字符串和不定参进行字符串组织，得到的日志消息的字符串
            //  处理日志消息
            va_list ap;
            va_start(ap, fmt);
            char *res = nullptr;
            int ret = vasprintf(&res, fmt, ap);
            va_end(ap);
            if (ret == -1)
            {
                std::cout << "vasprintf failed!\n,日志消息格式化失败" << std::endl;
                return;
            }
            serialize(LogLevel::Level::INFO, file, line, res);
            // 释放内存
            free(res);
        }
        // 记录 WARN 级别日志。
        void warn(const char *file, size_t line, const char *fmt, ...)
        {
            // 通过传入的参数构造日志消息对象，进行日志的格式化，最终落地
            // 1.判断当前日志等级是否达到输出等级要求
            if (_limit_level.load() > LogLevel::Level::WARN)
            {
                return;
            }

            // 2.对fmt格式化字符串和不定参进行字符串组织，得到的日志消息的字符串
            //  处理日志消息
            va_list ap;
            va_start(ap, fmt);
            char *res = nullptr;
            int ret = vasprintf(&res, fmt, ap);
            va_end(ap);
            if (ret == -1)
            {
                std::cout << "vasprintf failed!\n,日志消息格式化失败" << std::endl;
                return;
            }
            serialize(LogLevel::Level::WARN, file, line, res);
            // 释放内存
            free(res);
        }
        // 记录 ERROR 级别日志。
        void error(const char *file, size_t line, const char *fmt, ...)
        {
            // 通过传入的参数构造日志消息对象，进行日志的格式化，最终落地
            // 1.判断当前日志等级是否达到输出等级要求
            if (_limit_level.load() > LogLevel::Level::ERROR)
            {
                return;
            }

            // 2.对fmt格式化字符串和不定参进行字符串组织，得到的日志消息的字符串
            //  处理日志消息
            va_list ap;
            va_start(ap, fmt);
            char *res = nullptr;
            int ret = vasprintf(&res, fmt, ap);
            va_end(ap);
            if (ret == -1)
            {
                std::cout << "vasprintf failed!\n,日志消息格式化失败" << std::endl;
                return;
            }
            serialize(LogLevel::Level::ERROR, file, line, res);
            // 释放内存
            free(res);
        }
        // 记录 FATAL 级别日志。
        void fatal(const char *file, size_t line, const char *fmt, ...)
        {
            // 通过传入的参数构造日志消息对象，进行日志的格式化，最终落地
            // 1.判断当前日志等级是否达到输出等级要求
            if (_limit_level.load() > LogLevel::Level::FATAL)
            {
                return;
            }

            // 2.对fmt格式化字符串和不定参进行字符串组织，得到的日志消息的字符串
            //  处理日志消息
            va_list ap;
            va_start(ap, fmt);
            char *res = nullptr;
            int ret = vasprintf(&res, fmt, ap);
            va_end(ap);
            if (ret == -1)
            {
                std::cout << "vasprintf failed!\n,日志消息格式化失败" << std::endl;
                return;
            }
            serialize(LogLevel::Level::FATAL, file, line, res);
            // 释放内存
            free(res);
        }

    protected:
        // 组装日志上下文、执行格式化，再交给同步或异步日志器输出。
        void serialize(LogLevel::Level level, const char *file, size_t line, const char *str)
        {
            // 3.构造日志消息LogMsg对象
            LogMsg msg(level, line, file, _logger_name, str);
            // 4.通过格式化工具对LogMsg对象进行格式化，得到格式化后的日志消息字符串
            std::stringstream ss;
            _formatter->format(ss, msg);
            // 5.进行日志落地输出
            std::string log_msg = ss.str();
            log(log_msg.c_str(), log_msg.size());
        }
        // 将已经格式化的日志数据交给具体日志器处理。
        virtual void log(const char *date, size_t size) = 0;

    protected:
        std::mutex _mutex;                         // 保护同步写日志过程的互斥锁。
        std::string _logger_name;                  // 当前日志器名称。
        std::atomic<LogLevel::Level> _limit_level; // 最低输出等级。
        Formatter::ptr _formatter;                 // 日志格式化器。
        std::vector<LogSink::ptr> _sinks;          // 日志需要写入的所有目标。
    };
    // 同步日志器：在调用线程中直接完成日志输出。
    class SyncLogger : public Logger
    {
    public:
        using ptr = std::shared_ptr<SyncLogger>; // 同步日志器的共享指针类型。
        SyncLogger(const std::string &logger_name, LogLevel::Level limit_level, Formatter::ptr formatter, const std::vector<LogSink::ptr> &sinks)
            : Logger(logger_name, limit_level, formatter, sinks) {
              };

    private:
        // 同步日志器，是将日志直接通过落地模块句柄进行日志落地输出
        //  将一条日志依次写入所有落地器。
        void log(const char *date, size_t size) override
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_sinks.empty())
            {
                return;
            }
            for (auto &sink : _sinks)
            {
                sink->log(date, size);
            }
        }
    };
    // 异步日志器：前台线程写入缓冲区，后台线程负责真正的日志落地。
    class AsyncLogger : public Logger
    {
    public:
        // 创建异步工作器，并将 realLog 注册为后台消费回调。
        AsyncLogger(const std::string &logger_name,
                    LogLevel::Level limit_level,
                    Formatter::ptr formatter,
                    const std::vector<LogSink::ptr> &sinks,
                    AsyncType Looper_type)
            : Logger(logger_name, limit_level, formatter, sinks),
              _looper(std::make_shared<AsyncLooper>(std::bind(&AsyncLogger::realLog, this, std::placeholders::_1), Looper_type)) {
              };
        // 将格式化后的日志提交给异步缓冲区。
        void log(const char *date, size_t size) override
        {
            _looper->push(date, size);
        }
        // 后台线程回调：将一批缓冲区数据写入所有落地目标。
        void realLog(Buffer &buffer)
        {
            if (_sinks.empty())
            {
                return;
            }
            for (auto &sink : _sinks)
            {
                sink->log(buffer.begin(), buffer.readAbleSize());
            }
        }

    private:
        AsyncLooper::ptr _looper; // 异步缓冲区及后台消费线程。
    };
    // 日志器类型，用于在建造阶段选择同步或异步实现。
    enum class LoggerType
    {
        LOGGER_SYNC,  // 同步日志器
        LOGGER_ASYNC, // 异步日志器
    };
    /*使用建造者模式来建造日志器，而不是让用户直接去构造日志器，简化用户的使用复杂度*/
    // 1.抽象一个日志器建造者类（完成日志器对象所需零部件的构造&日志器的构建）
    //  1.1。设置日志器类型
    //  1.2.将不同日志器的创建放在同一个日志器建造者类中完成
    class LoggerBuilder
    {
    public:
        using ptr = std::shared_ptr<LoggerBuilder>; // 日志器建造者的共享指针类型。
        // 默认创建同步、DEBUG 等级、安全缓冲模式的日志器。
        LoggerBuilder() : _logger_type(LoggerType::LOGGER_SYNC),
                          _limit_level(LogLevel::Level::DEBUG),
                          _looper_type(AsyncType::ASYNC_SAFE)
        {
        }
        virtual ~LoggerBuilder() = default;
        // 设置日志器类型
        void buildLoggerType(LoggerType logger_type)
        {
            _logger_type = logger_type;
        }
        // 允许异步缓冲区自动扩容，适合性能测试场景。
        void buildEnableUnSafeAsync() { _looper_type = AsyncType::ASYNC_UNSAFE; }
        // 设置日志器名称
        void buildLoggerName(const std::string &logger_name)
        {
            _logger_name = logger_name;
        }
        // 设置日志器的最低输出等级
        void buildLimitLevel(LogLevel::Level limit_level)
        {
            _limit_level = limit_level;
        }
        // 设置日志器的格式化规则
        void buildFormatter(const std::string &pattern)
        {
            _formatter = std::make_shared<Formatter>(pattern);
        }
        // 设置日志器的落地目标
        template <typename SinkType, typename... Args>
        void buildSinks(Args &&...args)
        {
            LogSink::ptr psink = SinkFactory<SinkType>::create(std::forward<Args>(args)...);
            _sinks.push_back(psink);
        }
        // 构建日志器对象
        virtual Logger::ptr build() = 0;

    protected:
        AsyncType _looper_type;            // 异步日志器的缓冲策略。
        LoggerType _logger_type;          // 日志器类型
        std::string _logger_name;         // 日志器名称
        LogLevel::Level _limit_level;     // 日志器最低输出等级
        Formatter::ptr _formatter;        // 日志器格式化规则
        std::vector<LogSink::ptr> _sinks; // 日志器落地目标
    };
    // 局部日志器建造者：创建日志器，但不注册到全局管理器。
    class LocalLoggerBuilder : public LoggerBuilder
    {
    public:
        Logger::ptr build() override
        {
            assert(_logger_name.empty() == false && "日志器名称不能为空");
            if (_formatter.get() == nullptr)
            {
                _formatter = std::make_shared<Formatter>();
            }
            if (_sinks.empty())
            {
                buildSinks<StdoutSink>();
            }
            if (_logger_type == LoggerType::LOGGER_SYNC)
            {
                return std::make_shared<SyncLogger>(_logger_name, _limit_level, _formatter, _sinks);
            }
            else
            {
                return std::make_shared<AsyncLogger>(_logger_name, _limit_level, _formatter, _sinks, _looper_type);
            }
        }
    };

    // 全局日志器管理器：按名称保存日志器，并提供默认 root 日志器。
    class LoggerManager
    {
    public:
        // 获取进程内唯一的日志器管理器实例。
        static LoggerManager &getInstance()
        {
            static LoggerManager instance;
            return instance;
        }
        // 将日志器注册到管理器；名称重复时不覆盖旧日志器。
        void addLogger(Logger::ptr logger)
        {
            if (hasLogger(logger->name()))
            {
                std::cout << "logger name already exists!" << std::endl;
                return;
            }
            std::lock_guard<std::mutex> lock(_mutex);
            _loggers.insert({logger->name(), logger});
        }

        // 判断指定名称的日志器是否已经注册。
        bool hasLogger(const std::string &name)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _loggers.find(name);
            return it != _loggers.end();
        }
        // 按名称获取日志器；不存在时返回空指针。
        Logger::ptr getLogger(const std::string &name)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            auto it = _loggers.find(name);
            if (it != _loggers.end())
            {
                return it->second;
            }
            else
            {
                std::cout << "logger name not found!" << std::endl;
                return nullptr;
            }
        }
        // 获取默认 root 日志器。
        Logger::ptr rootLogger()
        {
            return _root_logger;
        }

    private:
        // 创建并注册默认 root 日志器。
        LoggerManager()
        {
            std::unique_ptr<duallog::LoggerBuilder> builder(new duallog::LocalLoggerBuilder());
            builder->buildLoggerName("root");
            _root_logger = builder->build();
            _loggers.insert({"root", _root_logger});
        }

    private:
        std::mutex _mutex;
        Logger::ptr _root_logger;                              // 默认日志器
        std::unordered_map<std::string, Logger::ptr> _loggers; // 所有日志器
    };
    // 全局日志器建造者：创建日志器，并自动注册到 LoggerManager。
    class GlobalLoggerBuilder : public LoggerBuilder
    {
    public:
        // 根据当前配置构建日志器，并将它加入全局管理器。
        Logger::ptr build() override
        {
            assert(_logger_name.empty() == false && "日志器名称不能为空");
            if (_formatter.get() == nullptr)
            {
                _formatter = std::make_shared<Formatter>();
            }
            if (_sinks.empty())
            {
                buildSinks<StdoutSink>();
            }
            Logger::ptr logger;
            if (_logger_type == LoggerType::LOGGER_SYNC)
            {
                logger = std::make_shared<SyncLogger>(_logger_name, _limit_level, _formatter, _sinks);
            }
            else
            {
                logger = std::make_shared<AsyncLogger>(_logger_name, _limit_level, _formatter, _sinks, _looper_type);
            }
            LoggerManager::getInstance().addLogger(logger);
            return logger;
        }
    };

}

#endif
