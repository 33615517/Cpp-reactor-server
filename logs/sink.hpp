/*日志落地模块的实现
    1. 抽象落地基类
    2. 派生子类（根据不同的落地方向进行派生）
    3. 使用工厂模式进行创建与表示的分离
*/
#ifndef DUALLOG_SINK_HPP
#define DUALLOG_SINK_HPP
#include "util.hpp"
#include <cstddef>
#include <iostream>
#include <sstream>
#include <memory>
#include <fstream>
#include <assert.h>
namespace duallog
{
    // 日志落地接口：所有输出目标都实现 log 方法。
    class LogSink
    {
    public:
        using ptr = std::shared_ptr<LogSink>; // 日志落地器的共享指针类型。
        // 创建日志落地器基类对象。
        LogSink() {}
        // 通过基类指针销毁派生落地器时，保证正确调用派生类析构函数。
        virtual ~LogSink() = default;
        // 输出一段日志数据；date 指向数据起始位置，size 表示字节数。
        virtual void log(const char *date, size_t size) = 0;
    };
    // 落地方向：标准输出
    class StdoutSink : public LogSink
    {
    public:
        // 将日志消息写入到标准输出
        void log(const char *date, size_t size) override
        {
            std::cout.write(date, size);
        }
    };
    // 落地方向：文件输出
    class FileSink : public LogSink
    {
    public:
        // 创建固定文件落地器，并以追加方式打开目标文件。
        FileSink(const std::string &filename) : _filename(filename)
        {
            // 1.创建日志文件所在的目录
            duallog::util::File::createDirectory(duallog::util::File::path(_filename));
            // 2.创建并打开日志文件
            _ofs.open(_filename, std::ios::binary | std::ios::app);
            assert(_ofs.is_open() && "日志文件打开失败，请检查文件路径是否正确");
        }
        // 将指定长度的日志数据追加写入文件。
        void log(const char *date, size_t size) override
        {
            _ofs.write(date, size);
            assert(_ofs.good() && "日志文件写入失败，请检查文件路径是否正确");
        }

    private:
        std::string _filename; // 日志文件的完整路径。
        std::ofstream _ofs;    // 目标文件输出流。
    };
    // 落地方式：滚动文件（以文件大小为单位进行滚动）
    class RollBySizeSink : public LogSink
    {
    public:
        // 创建按大小滚动的文件落地器，max_size 是单个文件的最大字节数。
        RollBySizeSink(const std::string &basename, size_t max_size) : _basename(basename), _max_size(max_size), _cur_size(0), _name_count(0)
        {
            // 1.创建日志文件所在的目录
            std::string filename = createNewFile();
            duallog::util::File::createDirectory(duallog::util::File::path(filename));
            // 2.创建并打开日志文件
            _ofs.open(filename, std::ios::binary | std::ios::app);
            assert(_ofs.is_open() && "日志文件打开失败，请检查文件路径是否正确");
        }
        // 写入日志；写入后将超出上限时，先切换到一个新文件。
        void log(const char *date, size_t size) override
        {
            if (_cur_size + size > _max_size)
            {
                // 1.关闭当前文件
                _ofs.close();
                // 2.创建新文件
                std::string filename = createNewFile();
                duallog::util::File::createDirectory(duallog::util::File::path(filename));
                // 3.打开新文件
                _ofs.open(filename, std::ios::binary | std::ios::app);
                assert(_ofs.is_open() && "日志文件打开失败，请检查文件路径是否正确");
                // 4.重置当前文件大小
                _cur_size = 0;
                //sleep(1); // 等待1秒钟，确保文件名不同
            }
            _ofs.write(date, size);
            assert(_ofs.good() && "日志文件写入失败，请检查文件路径是否正确");
            _cur_size += size;
        }

    private:
        // 根据基础文件名、当前时间和序号生成新的日志文件名。
        std::string createNewFile()
        {
            // 获取系统时间，以时间来构造文件名扩展名
            time_t t = util::Date::now();
            struct tm lt;
            localtime_r(&t, &lt);

            std::string filename= _basename+std::to_string(lt.tm_year + 1900) + "-" + std::to_string(lt.tm_mon + 1) + "-" + std::to_string(lt.tm_mday) + "-" + std::to_string(lt.tm_hour) + "-" + std::to_string(lt.tm_min) + "-" + std::to_string(lt.tm_sec) + "-" + std::to_string(_name_count++) +".log";

            return filename;
        }

    private:
        // 通过基础文件名 + 扩展文件名（以时间生成）组成一个实际的当前输出文件名
        size_t _name_count;    // 同一落地器已经生成的文件序号。
        std::string _basename; // 滚动日志文件的基础名称或路径前缀。
        std::ofstream _ofs;    // 当前日志文件的输出流。
        size_t _max_size; // 记录当前文件的最大大小，当前超过大小就要切换文件
        size_t _cur_size; // 记录当前文件的大小，当前超过大小就要切换文件
    };

    template <typename SinkType>
    // 日志落地器工厂：根据 SinkType 创建对应的共享对象。
    class SinkFactory
    {
    public:
        template <typename... Args>
        // 转发构造参数并创建指定类型的日志落地器。
        static LogSink::ptr create(Args &&...args)
        {
            return std::make_shared<SinkType>(
                std::forward<Args>(args)...);
        }
    };
}

#endif
