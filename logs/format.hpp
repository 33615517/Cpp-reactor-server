#ifndef DUALLOG_FORMAT_HPP
#define DUALLOG_FORMAT_HPP

#include "util.hpp"
#include "level.hpp"
#include "message.hpp"
#include <string>
#include <memory>
#include <vector>
#include <sstream>
#include <cassert>
namespace duallog
{
    // 抽象格式化子项基类
    // 所有具体格式化类都继承它，每个子类负责格式化日志中的一种内容
    class FormatItem
    {
    public:
        using ptr = std::shared_ptr<FormatItem>; // 格式化子项的共享指针类型。

        // 将对应的日志内容写入输出流中
        virtual void format(std::ostream &os, const LogMsg &msg) = 0;
    };

    // 派生格式化子项子类--消息，等级，时间，文件名，行号，线程ID，日志器名，制表符，换行，其他

    // 消息格式化子项：输出日志正文
    class MsgFormatItem : public FormatItem
    {
    public:
        // 输出日志正文。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            out << msg._payload;
        }
    };

    // 日志等级格式化子项：输出 DEBUG、INFO、WARN、ERROR 等
    class LevelFormatItem : public FormatItem
    {
    public:
        // 输出日志等级名称。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            out << LogLevel::toString(msg._level);
        }
    };

    // 时间格式化子项：按照指定格式输出日志时间
    class TimeFormatItem : public FormatItem
    {
    public:
        // 保存时间格式；默认只输出时、分、秒。
        TimeFormatItem(const std::string &fmt = "%H:%M:%S")
            : _time_fmt(fmt.empty() ? "%H:%M:%S" : fmt)
        {
        }

        // 按照 _time_fmt 格式化日志产生时间。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            struct tm t; // 保存转换后的本地时间。

            // 将时间戳转换成本地时间
            localtime_r(&msg._ctime, &t);

            char buf[64]; // 保存 strftime 生成的时间字符串。

            // 按照 _time_fmt 指定的格式生成时间字符串
            strftime(buf, sizeof(buf), _time_fmt.c_str(), &t);

            out << buf;
        }

    private:
        std::string _time_fmt; //%H %M %S
    };

    // 文件名格式化子项：输出日志所在的源文件名
    class FileFormatItem : public FormatItem
    {
    public:
        // 输出产生这条日志的源文件名。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            out << msg._file;
        }
    };

    // 线程ID格式化子项
    // 注意：当前代码实际输出的是 _line，也就是行号
    class ThreadFormatItem : public FormatItem
    {
    public:
        // 输出线程信息；注意：当前实现实际输出的是源码行号。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            out << msg._line;
        }
    };

    // 日志器名称格式化子项：输出当前 Logger 的名字
    class LoggerFormatItem : public FormatItem
    {
    public:
        // 输出日志器名称。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            out << msg._logger;
        }
    };

    // 制表符格式化子项：输出一个 \t
    class TabFormatItem : public FormatItem
    {
    public:
        // 输出一个制表符，用于分隔日志字段。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            out << "\t";
        }
    };

    // 换行格式化子项：输出一个 \n
    class NewLineFormatItem : public FormatItem
    {
    public:
        // 输出一个换行符，结束当前日志行。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            out << "\n";
        }
    };

    // 其他字符串格式化子项：原样输出普通字符串
    // 比如 "["、"]"、"-" 等固定内容
    class OtherFormatItem : public FormatItem
    {
    public:
        // 保存需要原样输出的普通字符串。
        OtherFormatItem(const std::string &str)
            : _str(str)
        {
        }

        // 输出构造时保存的普通字符串。
        void format(std::ostream &out, const LogMsg &msg) override
        {
            out << _str;
        }

    private:
        std::string _str; // 需要原样输出的文本。
    };
    /*
    %d 表示日期，包含子格式 {%H:%M:%S}
    %t 表示线程ID
    %c 表示日志器名称
    %f 表示源码文件名
    %l 表示源码行号
    %p 表示日志级别
    %T 表示制表符缩进
    %m 表示主体消息
    %n 表示换行
*/
    // 日志格式化器：解析 pattern，并按顺序组合各个格式化子项。
    class Formatter
    {
    public:
        using ptr = std::shared_ptr<Formatter>; // 格式化器的共享指针类型。
        // 保存格式规则，并将其解析成一组格式化子项。
        Formatter(const std::string &pattern = "[%d{%H:%M:%S}][%t] [%c][%f:%l]%T%m%n")
            : _pattern(pattern)
        {
            bool ret = parsePattern();
            assert(ret);
        }
        // 按照格式规则把日志消息写入指定输出流。
        void format(std::ostream &out, const LogMsg &msg)
        {
            for (auto &item : _items)
            {
                item->format(out, msg);
            }
        }
        // 将格式化结果作为字符串返回。
        std::string format(LogMsg &msg)
        {
            std::ostringstream oss;
            format(oss, msg);
            return oss.str();
        }

    private:
        // 解析格式化规则字符串
        bool parsePattern()
        {
            // 1. 对格式化规则字符串进行解析
            // abcde[%d{%H:%M:%S}][%p]%T%m%n
            std::vector<std::pair<std::string, std::string>> fmt_order; // 按出现顺序保存标记和参数。
            size_t pos = 0;                                             // 当前解析位置。

            std::string key, val; // key 是格式标记，val 是标记参数或普通文本。
            while (pos < _pattern.size())
            {
                // 1.处理原始字符串--判断是否为%，不是就是原始字符
                if (_pattern[pos] != '%')
                {
                    val.push_back(_pattern[pos]);
                    pos++;
                    continue;
                }
                // 能走下来就代表pos位置是%，需要判断pos+1位置是否越界，且是否为%，如果是%%就输出%，否则就是格式化标记
                if (pos + 1 < _pattern.size() && _pattern[pos + 1] == '%')
                {
                    val.push_back('%');
                    pos += 2;
                    continue;
                }
                // 能走下来，代表%后面是个格式化字符，代表着一个格式化标记的开始，先把%后面的字符作为key，后面可能跟着{}，{}里面是value
                if (val.empty() == false)
                {
                    fmt_order.push_back(std::make_pair("", val));
                }
                val.clear();
                pos += 1;
                key = _pattern[pos];
                bool error_flag = false; // 标记带参数的格式项是否完整。
                if (pos + 1 < _pattern.size() && _pattern[pos + 1] == '{')
                {
                    error_flag = true;
                    pos += 2;
                    while (pos < _pattern.size() && _pattern[pos] != '}')
                    {
                        val.push_back(_pattern[pos]);
                        pos++;
                    }
                    // 走到末尾跳出循环，则代表没有遇到}，说明格式化规则字符串有误
                    if (pos == _pattern.size())
                    {
                        error_flag = false;
                        std::cout << "格式化规则字符串有误，缺少}，请检查" << std::endl;
                        return false;
                    }
                    pos++; // 因为遇到了}，所以pos需要+1
                }
                else
                    pos++; // 没有{}，所以pos需要+1
                fmt_order.push_back(std::make_pair(key, val));
                key.clear();
                val.clear();
            }
            if (!val.empty())
            {
                fmt_order.push_back(std::make_pair("", val));
            }
            // 2. 根据解析得到的数据初始化格式化子项数组成员
            for (auto &kv : fmt_order)
            {
                _items.push_back(createFormatItem(kv.first, kv.second));
            }
            return true;
        }
        // 根据不同的格式化标记创建对应的 FormatItem 对象
        FormatItem::ptr createFormatItem(const std::string &fmt, const std::string &value)
        {
            if (fmt == "d")
            {
                return std::make_shared<TimeFormatItem>(value);
            }
            else if (fmt == "t")
            {
                return std::make_shared<ThreadFormatItem>();
            }
            else if (fmt == "c")
            {
                return std::make_shared<LoggerFormatItem>();
            }
            else if (fmt == "f")
            {
                return std::make_shared<FileFormatItem>();
            }
            else if (fmt == "l")
            {
                return std::make_shared<ThreadFormatItem>();
            }
            else if (fmt == "p")
            {
                return std::make_shared<LevelFormatItem>();
            }
            else if (fmt == "T")
            {
                return std::make_shared<TabFormatItem>();
            }
            else if (fmt == "m")
            {
                return std::make_shared<MsgFormatItem>();
            }
            else if (fmt == "n")
            {
                return std::make_shared<NewLineFormatItem>();
            }
            else if (fmt.empty() && !value.empty())
            {
                return std::make_shared<OtherFormatItem>(value);
            }
            std::cout << "不支持的格式化标记：%" << fmt << std::endl;
            abort();//立刻异常终止当前程序
            return FormatItem::ptr(nullptr);
        }

    private:
        std::string _pattern;                // 原始格式化规则字符串。
        std::vector<FormatItem::ptr> _items; // 按输出顺序保存解析后的格式化子项。
    };
}

#endif
