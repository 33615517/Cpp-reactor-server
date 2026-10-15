
#include "../server.hpp"
#include <sys/stat.h>
#include <regex>
#include <charconv>
#include <filesystem>
#include <system_error>

#define DEFALT_TIMEOUT 30
// HTTP 协议层：工具函数、请求/响应数据，以及基于 Buffer 的分阶段请求解析。
// 网络收发由 server.hpp 提供，路由分发和响应报文组织由 HttpServer 完成。

std::unordered_map<int, std::string> _statu_msg = {
    {100,  "Continue"},
    {101,  "Switching Protocol"},
    {102,  "Processing"},
    {103,  "Early Hints"},
    {200,  "OK"},
    {201,  "Created"},
    {202,  "Accepted"},
    {203,  "Non-Authoritative Information"},
    {204,  "No Content"},
    {205,  "Reset Content"},
    {206,  "Partial Content"},
    {207,  "Multi-Status"},
    {208,  "Already Reported"},
    {226,  "IM Used"},
    {300,  "Multiple Choice"},
    {301,  "Moved Permanently"},
    {302,  "Found"},
    {303,  "See Other"},
    {304,  "Not Modified"},
    {305,  "Use Proxy"},
    {306,  "unused"},
    {307,  "Temporary Redirect"},
    {308,  "Permanent Redirect"},
    {400,  "Bad Request"},
    {401,  "Unauthorized"},
    {402,  "Payment Required"},
    {403,  "Forbidden"},
    {404,  "Not Found"},
    {405,  "Method Not Allowed"},
    {406,  "Not Acceptable"},
    {407,  "Proxy Authentication Required"},
    {408,  "Request Timeout"},
    {409,  "Conflict"},
    {410,  "Gone"},
    {411,  "Length Required"},
    {412,  "Precondition Failed"},
    {413,  "Payload Too Large"},
    {414,  "URI Too Long"},
    {415,  "Unsupported Media Type"},
    {416,  "Range Not Satisfiable"},
    {417,  "Expectation Failed"},
    {418,  "I'm a teapot"},
    {421,  "Misdirected Request"},
    {422,  "Unprocessable Entity"},
    {423,  "Locked"},
    {424,  "Failed Dependency"},
    {425,  "Too Early"},
    {426,  "Upgrade Required"},
    {428,  "Precondition Required"},
    {429,  "Too Many Requests"},
    {431,  "Request Header Fields Too Large"},
    {451,  "Unavailable For Legal Reasons"},
    {501,  "Not Implemented"},
    {502,  "Bad Gateway"},
    {503,  "Service Unavailable"},
    {504,  "Gateway Timeout"},
    {505,  "HTTP Version Not Supported"},
    {506,  "Variant Also Negotiates"},
    {507,  "Insufficient Storage"},
    {508,  "Loop Detected"},
    {510,  "Not Extended"},
    {511,  "Network Authentication Required"}
};

std::unordered_map<std::string, std::string> _mime_msg = {
    {".aac",        "audio/aac"},
    {".abw",        "application/x-abiword"},
    {".arc",        "application/x-freearc"},
    {".avi",        "video/x-msvideo"},
    {".azw",        "application/vnd.amazon.ebook"},
    {".bin",        "application/octet-stream"},
    {".bmp",        "image/bmp"},
    {".bz",         "application/x-bzip"},
    {".bz2",        "application/x-bzip2"},
    {".csh",        "application/x-csh"},
    {".css",        "text/css"},
    {".csv",        "text/csv"},
    {".doc",        "application/msword"},
    {".docx",       "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".eot",        "application/vnd.ms-fontobject"},
    {".epub",       "application/epub+zip"},
    {".gif",        "image/gif"},
    {".htm",        "text/html"},
    {".html",       "text/html"},
    {".ico",        "image/vnd.microsoft.icon"},
    {".ics",        "text/calendar"},
    {".jar",        "application/java-archive"},
    {".jpeg",       "image/jpeg"},
    {".jpg",        "image/jpeg"},
    {".js",         "text/javascript"},
    {".json",       "application/json"},
    {".jsonld",     "application/ld+json"},
    {".mid",        "audio/midi"},
    {".midi",       "audio/x-midi"},
    {".mjs",        "text/javascript"},
    {".mp3",        "audio/mpeg"},
    {".mpeg",       "video/mpeg"},
    {".mpkg",       "application/vnd.apple.installer+xml"},
    {".odp",        "application/vnd.oasis.opendocument.presentation"},
    {".ods",        "application/vnd.oasis.opendocument.spreadsheet"},
    {".odt",        "application/vnd.oasis.opendocument.text"},
    {".oga",        "audio/ogg"},
    {".ogv",        "video/ogg"},
    {".ogx",        "application/ogg"},
    {".otf",        "font/otf"},
    {".png",        "image/png"},
    {".pdf",        "application/pdf"},
    {".ppt",        "application/vnd.ms-powerpoint"},
    {".pptx",       "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".rar",        "application/x-rar-compressed"},
    {".rtf",        "application/rtf"},
    {".sh",         "application/x-sh"},
    {".svg",        "image/svg+xml"},
    {".swf",        "application/x-shockwave-flash"},
    {".tar",        "application/x-tar"},
    {".tif",        "image/tiff"},
    {".tiff",       "image/tiff"},
    {".ttf",        "font/ttf"},
    {".txt",        "text/plain"},
    {".vsd",        "application/vnd.visio"},
    {".wav",        "audio/wav"},
    {".weba",       "audio/webm"},
    {".webm",       "video/webm"},
    {".webp",       "image/webp"},
    {".woff",       "font/woff"},
    {".woff2",      "font/woff2"},
    {".xhtml",      "application/xhtml+xml"},
    {".xls",        "application/vnd.ms-excel"},
    {".xlsx",       "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".xml",        "application/xml"},
    {".xul",        "application/vnd.mozilla.xul+xml"},
    {".zip",        "application/zip"},
    {".3gp",        "video/3gpp"},
    {".3g2",        "video/3gpp2"},
    {".7z",         "application/x-7z-compressed"}
};


// 无状态的 HTTP 辅助工具，供请求解析和静态资源处理使用。
class Util
{
public:
    // 按非空分隔串 sep 分割 src，跳过空片段并追加到 arry；返回 arry 的总元素数。
    static size_t Split(const std::string &src, const std::string &sep, std::vector<std::string> *arry)
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
                // 连续分隔符或开头的分隔符不产生空字符串。
                offset = pos + sep.size();
                continue;
            }
            arry->push_back(src.substr(offset, pos - offset));
            offset = pos + sep.size();
        }

        return arry->size();
    }
    // 以二进制方式读取整个文件到输出字符串 buf，失败时记录日志。
    static bool ReadFile(const std::string &filename, std::string &buf)
    {
        std::ifstream ifs(filename, std::ios::binary);
        if (ifs.is_open() == false)
        {
            DUALLOG_DEBUG("open %s file!!", filename.c_str());
            return false;
        }
        size_t fsize = 0;
        // 先定位到文件末尾获取大小，再回到开头读取。
        ifs.seekg(0, ifs.end);
        fsize = ifs.tellg();
        ifs.seekg(0, ifs.beg);
        buf.resize(fsize);
        ifs.read(buf.data(), fsize);
        if (ifs.good() == false)
        {
            DUALLOG_DEBUG("READ %s FILE FAILED!!", filename.c_str());
            return false;
        }
        ifs.close();
        return true;
    }
    // 将 buf 以二进制方式写入文件；trunc 会覆盖文件原有内容。
    static bool WriteFile(const std::string &filename, const std::string &buf)
    {
        std::ofstream ofs(filename, std::ios::binary | std::ios::trunc);
        if (ofs.is_open() == false)
        {
            DUALLOG_DEBUG("READ %s FILE FAILED!!", filename.c_str());
            return false;
        }
        ofs.write(buf.c_str(), buf.size());
        if (ofs.good() == false)
        {
            // 写入失败时返回 false，由调用方决定如何处理。
            DUALLOG_DEBUG("WRITE %s FILE FAILED!!", filename.c_str());
            ofs.close();
            return false;
        }
        ofs.close();
        return true;
    }
    // 将 URL 中需转义的字节编码为 %HH，convert_space_to_plus 控制空格分支。
    static std::string UrlEncode(const std::string url, bool convert_space_to_plus)
    {
        std::string res;
        for (auto &c : url)
        {
            if (c == '.' || c == '-' || c == '_' || c == '~' || isalnum(c))
            {
                res += c;
                continue;
            }
            else if (c == ' ' && convert_space_to_plus)
            {
                res += '+';
                continue;
            }
            // 剩下的字符都是需要编码成为%HH格式
            char tmp[4] = {0};
            snprintf(tmp, sizeof(tmp), "%%%02X", c);
            res += tmp;
        }
        return res;
    }
    // 为 %HH 解码换算单个字符的数值；调用方应传入合法十六进制字符。
    // 当前实现也接受 g～z/G～Z，未对非法编码进行严格校验。
    static char HEXTOI(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        else if (c >= 'a' && c <= 'z')
            return c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z')
            return c - 'A' + 10;
        return -1;
    }
    // 将 %HH 还原为一个字节；解析查询参数时可选择把 '+' 转为空格。
    static std::string UrlDecode(const std::string url, bool convert_plus_to_space)
    {
        // 遇到了%，则将紧随其后的2个字符，转换为数字，第一个数字左移4位，然后加上第二个数字
        // + -> 2b  %2b -> 2 << 4 + 11
        std::string res;
        for (int i = 0; i < url.size(); i++)
        {
            if (url[i] == '+' && convert_plus_to_space == true)
            {
                res += ' ';
                continue;
            }
            if (url[i] == '%' && (i + 2) < url.size())
            {
                char v1 = HEXTOI(url[i + 1]);
                char v2 = HEXTOI(url[i + 2]);
                char v = v1 * 16 + v2;
                res += v;
                i += 2;
                continue;
            }
            res += url[i];
        }
        return res;
    }
    // 查询状态码对应的原因短语，用于生成 HTTP 响应行；未知值返回 "Unknow"。
    static std::string StatuDesc(int statu)
    {
        auto it = _statu_msg.find(statu);
        if (it != _statu_msg.end())
        {
            return it->second;
        }
        return "Unknow";
    }
    // 根据最后一个文件后缀查询 MIME 类型；无后缀或未知后缀按二进制流处理。
    static std::string ExtMime(const std::string &filename)
    {
        // 只取最后一个点之后的后缀，例如 a.b.txt 对应 .txt；当前匹配区分大小写。
        size_t pos = filename.find_last_of('.');
        if (pos == std::string::npos)
        {
            return "application/octet-stream";
        }
        std::string ext = filename.substr(pos);
        auto it = _mime_msg.find(ext);
        if (it == _mime_msg.end())
        {
            return "application/octet-stream";
        }
        return it->second;
    }
    // 通过 stat() 判断路径是否指向目录；查询失败时返回 false。
    static bool IsDirectory(const std::string &filename)
    {
        struct stat st;
        int ret = stat(filename.c_str(), &st);
        if (ret < 0)
            return false;
        return S_ISDIR(st.st_mode);
    }
    // 通过 stat() 判断路径是否指向普通文件；查询失败时返回 false。
    static bool IsRegular(const std::string &filename)
    {
        struct stat st;
        int ret = stat(filename.c_str(), &st);
        if (ret < 0)
            return false;
        return S_ISREG(st.st_mode);
    }
    // HTTP请求的资源路径有效性判断
    // /index.html --- 前边的/叫做相对根目录，映射的是某个服务器上的子目录
    // 想表达的意思就是，客户端只能请求相对根目录中的资源，其他地方的资源都不予理会
    // /../login，这个路径中的..会让路径的查找跑到相对根目录之外，这是不合理的，不安全的
    static bool ValidPath(const std::string &path)
    {
        // 思想:按照/进行路径分割，根据有多少子目录，计算目录深度，有多少层，深度不能小于0
        std::vector<std::string> subdir;
        Split(path, "/", &subdir);
        // 从相对根目录开始统计深度，向上越过根目录时拒绝路径。
        int level = 0;
        for (auto &dir : subdir)
        {
            if (dir == "..")
            {
                level--;
                // 任意一层走出相对根目录，就认为有问题
                if (level < 0)
                    return false;
            }
            else
                level++;
        }
        return true;
    }
};

// 一次 HTTP 请求的解析结果；HttpContext 填充字段，业务层读取这些字段处理请求。
// 头字段和查询参数按原始键存储，查找区分大小写，重复插入时保留首次值。
class HttpRequest
{
public:
    std::string _method;                                   // 请求方法，解析后统一为大写
    std::string _path;                                     // 解码后的资源路径，不含查询字符串
    std::string _version = "HTTP/1.1";                     // 请求行中的 HTTP 协议版本
    std::string _body;                                     // 已累积接收的请求正文
    std::smatch _matches;                                  // 预留的资源路径匹配结果，当前解析器未填充
    std::unordered_map<std::string, std::string> _headers; // 头字段名称到字段值的映射
    std::unordered_map<std::string, std::string> _params;  // URL 解码后的查询参数键值对

public:
    // 清空上一请求的数据，并把协议版本恢复为 HTTP/1.1，供上下文复用。
    void ReSet()
    {
        _method.clear();
        _path.clear();
        _version = "HTTP/1.1";
        _body.clear();
        std::smatch match;
        _matches.swap(match);
        _headers.clear();
        _params.clear();
    }
    // 插入头字段；insert() 不覆盖已经存在的同名字段。
    void SetHeader(const std::string &key, const std::string &val)
    {
        _headers.insert(std::make_pair(key, val));
    }
    // 判断是否存在指定头部字段
    bool HasHeader(const std::string &key) const
    {
        auto it = _headers.find(key);
        if (it == _headers.end())
        {
            return false;
        }
        return true;
    }
    // 查询头字段，键不存在时返回空字符串。
    std::string GetHeader(const std::string &key) const
    {
        auto it = _headers.find(key);
        if (it == _headers.end())
        {
            return "";
        }
        return it->second;
    }
    // 插入一组查询参数；重复的参数名保留第一次插入的值。
    void SetParam(const std::string &key, const std::string &val)
    {
        _params.insert(std::make_pair(key, val));
    }
    // 判断是否存在指定名称的查询参数。
    bool HasParam(const std::string &key)
    {
        auto it = _params.find(key);
        if (it == _params.end())
        {
            return false;
        }
        return true;
    }
    // 获取指定查询参数的值，参数不存在时返回空字符串。
    std::string GetParam(const std::string &key)
    {

        auto it = _params.find(key);
        if (it == _params.end())
        {
            return "";
        }
        return it->second;
    }
    // 从 Content-Length 字段转换出正文长度；没有该字段时按无正文处理。
    size_t ContentLength()
    {
        bool ret = HasHeader("Content-Length");
        if (ret == false)
            return 0;
        std::string clen = GetHeader("Content-Length");
        return std::stol(clen);
    }
    // HTTP/1.1 默认使用长连接；HTTP/1.0 需要显式声明 keep-alive。
    bool Close() const
    {
        if (GetHeader("Connection") == "close")
        {
            return true;
        }
        return _version == "HTTP/1.0" && GetHeader("Connection") != "keep-alive";
    }
};

// HTTP 响应的数据容器，保存状态码、正文、头字段和重定向信息。
// 由 HttpServer 将数据序列化为 HTTP 报文。
class HttpResponse
{
public:
    int _statu;                                            // 响应状态码，默认 200
    bool _redirect_flag;                                   // 是否已设置重定向响应
    std::string _body;                                     // 响应正文，可以包含二进制数据
    std::string _redirect_url;                             // 重定向的目标 URL
    std::unordered_map<std::string, std::string> _headers; // 响应头字段的键值对

public:
    // 创建默认的成功响应，初始无正文且未设置重定向。
    HttpResponse() : _redirect_flag(false), _statu(200)
    {
    }
    // 创建指定状态码的响应。
    HttpResponse(int statu) : _redirect_flag(false), _statu(statu)
    {
    }
    // 清空响应内容，恢复默认的 200 状态，便于重复使用。
    void ReSet()
    {
        _statu = 200;
        _redirect_flag = false;
        _body.clear();
        _redirect_url.clear();
        _headers.clear();
    }
    // 插入响应头字段；已有同名字段时不会覆盖其值。
    void SetHeader(const std::string &key, const std::string &val)
    {
        _headers.insert(std::make_pair(key, val));
    }
    // 判断是否存在指定头部字段
    bool HasHeader(const std::string &key)
    {
        auto it = _headers.find(key);
        if (it == _headers.end())
        {
            return false;
        }
        return true;
    }
    // 获取指定响应头字段的值，不存在时返回空字符串。
    std::string GetHeader(const std::string &key)
    {
        auto it = _headers.find(key);
        if (it == _headers.end())
        {
            return "";
        }
        return it->second;
    }
    // 保存正文并插入 Content-Type；正文长度字段需要在生成报文时另行处理。
    void SetContent(const std::string &body, const std::string &type)
    {
        _body = body;
        SetHeader("Content-Type", type);
    }
    // 保存重定向状态和目标地址；当前只记录数据，尚未插入 Location 头字段。
    void SetRedirect(const std::string &url, int statu = 302)
    {
        _statu = statu;
        _redirect_flag = true;
        _redirect_url = url;
    }
    // 响应声明 Connection: close 时，在发送完响应后关闭连接。
    bool Close()
    {
        // 只判断精确匹配的 Connection 字段和值。
        if (HasHeader("Connection") == true && GetHeader("Connection") == "close")
        {
            return true;
        }
        return false;
    }
};

// 请求解析状态：请求行 -> 头字段 -> 正文 -> 完成；任一阶段失败则进入错误状态。
typedef enum
{
    RECV_HTTP_ERROR, // 格式错误或数据过长，停止推进解析
    RECV_HTTP_LINE,  // 等待/解析请求行
    RECV_HTTP_HEAD,  // 逐行解析头字段，直到遇到空行
    RECV_HTTP_BODY,  // 按 Content-Length 累积正文
    RECV_HTTP_OVER   // 当前请求接收完成
} HttpRecVStatu;
// 请求行和单行头字段的长度上限，包含获取到的行尾换行符。
#define MAX_LINE 8192

// 保存一个连接当前请求的解析进度，可作为 Connection 的 Any 上下文使用。
// 数据不足时保留状态和已解析内容，等待下一次消息回调继续推进。
// RecvHttp*() 返回 true 也可能只是等待更多数据，是否完成应查看 RecvStatu()。
class HttpContext
{
private:
    int _resp_statu;           // 默认 200；解析失败时记录应回复的错误状态码
    HttpRecVStatu _recv_statu; // 当前接收及解析的阶段状态
    HttpRequest _request;      // 已经解析得到的请求信息
private:
    // 尝试读取完整请求行；不完整时保留缓冲区，成功解析后才消费该行。
    bool RecvHttpLine(Buffer *buf)
    {
        if (_recv_statu != RECV_HTTP_LINE)
            return false;
        // 1.获取一行数据
        std::string line = buf->GetLine();
        // 2,需要考虑的一些要素：缓冲区中的数据不足一行，获取的一行数据量超大
        if (line.size() == 0)
        {
            // 缓冲区的数据不足一行，则需要判断缓冲区的可读数据长度，如果很长了都不足一行，这是有问题的
            if (buf->ReadAbleSize() > MAX_LINE)
            {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 414; // URL TOO LONG
                return false;
            }
            return true;
        }
        if (line.size() > MAX_LINE)
        {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 414; // URL TOO LONG
            return false;
        }
        bool ret = ParseHttpLine(line);
        if (ret == false)
            return false;
        // 确认请求行合法后再推进读下标，使后续解析从第一个头字段开始。
        buf->MoveReadOffset(line.size());
        _recv_statu = RECV_HTTP_HEAD;
        return true;
    }
    // 拆出方法、路径、可选查询串和 HTTP/1.0 或 HTTP/1.1 版本，并解析查询参数。
    bool ParseHttpLine(const std::string &line)
    {
        std::smatch matches;
        std::regex e("(GET|HEAD|POST|PUT|DELETE) ([^?]*)(?:\\?(.*))? (HTTP/1\\.[01])(?:\n|\r\n)?", std::regex::icase);
        bool ret = std::regex_match(line, matches, e);
        if (ret == false)
        {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400; // BAD REQUEST
            return false;
        }
        // 0 : GET /xingguichutu/login?user=xiaoming&pass=123123 HTTP/1.1
        // 1 : GET
        // 2 : /xingguichutu/login
        // 3 : user=xiaoming&pass=123123
        // 4 : HTTP/1.1
        // 请求方法的获取
        _request._method = matches[1];
        std::transform(_request._method.begin(), _request._method.end(), _request._method.begin(), ::toupper);
        // 资源路径的获取，需要进行URL解码操作，但是不需要+转空格
        _request._path = Util::UrlDecode(matches[2], false);
        // 协议版本的获取
        _request._version = matches[4];
        // 查询字符串的获取与处理
        std::vector<std::string> query_string_arry;
        std::string query_string = matches[3];
        // 查询字符串的格式 key=val&key=val....., 先以 & 符号进行分割，得到各个字串
        Util::Split(query_string, "&", &query_string_arry);
        // 针对各个字串，以 = 符号进行分割，得到key 和val， 得到之后也需要进行URL解码
        for (auto &str : query_string_arry)
        {
            size_t pos = str.find("=");
            if (pos == std::string::npos)
            {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 400; // BAD REQUEST
                return false;
            }
            std::string key = Util::UrlDecode(str.substr(0, pos), true);
            std::string val = Util::UrlDecode(str.substr(pos + 1), true);
            _request.SetParam(key, val);
        }
        return true;
    }
    // 逐行解析头字段，遇到空行后进入正文阶段；数据不足时等待后续接收。
    bool RecvHttpHead(Buffer *buf)
    {
        if (_recv_statu != RECV_HTTP_HEAD)
            return false;
        // 1.获取一行数据,直到遇到空行截止，头部的格式 key: val\r\nkey: val\r\n
        while (1)
        {
            std::string line = buf->GetLine();
            // 2,需要考虑的一些要素：缓冲区中的数据不足一行，获取的一行数据量超大
            if (line.size() == 0)
            {
                // 缓冲区的数据不足一行，则需要判断缓冲区的可读数据长度，如果很长了都不足一行，这是有问题的
                if (buf->ReadAbleSize() > MAX_LINE)
                {
                    _recv_statu = RECV_HTTP_ERROR;
                    _resp_statu = 414; // URL TOO LONG
                    return false;
                }
                return true;
            }
            if (line.size() > MAX_LINE)
            {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 414; // URL TOO LONG
                return false;
            }
            // GetLine() 只查看数据，完整头字段和末尾空行都需要消费。
            buf->MoveReadOffset(line.size());
            if (line == "\n" || line == "\r\n")
            {
                break;
            }
            bool ret = ParseHttpHead(line);
            if (ret == false)
                return false;
        }
        _recv_statu = RECV_HTTP_BODY;
        return true;
    }
    // 按冒号拆分头字段，并去掉值两端的空白及行尾换行符。
    bool ParseHttpHead(std::string &line)
    {
        // key: val\r\nkey: val\r\n....
        if (line.back() == '\n')
            line.pop_back(); // 末尾是换行则去掉换行字符
        if (line.back() == '\r')
            line.pop_back(); // 末尾是回车则去掉回车字符
        size_t pos = line.find(": ");
        if (pos == std::string::npos)
        {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400; //
            return false;
        }
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 2);
        _request.SetHeader(key, val);
        return true;
    }
    // 按 Content-Length 接收正文，可跨多次调用累积；不解析分块传输编码。
    bool RecvHttpBody(Buffer *buf)
    {
        if (_recv_statu != RECV_HTTP_BODY)
            return false;
        // 1.获取正文长度
        size_t content_length = _request.ContentLength();
        if (content_length == 0)
        {
            // 没有正文,则请求接收完毕
            _recv_statu = RECV_HTTP_OVER;
            return true;
        }
        // 2.当前已经接收了多少正文 ,其实就是往_request._body中放了多少数据
        size_t real_len = content_length - _request._body.size();
        // 3.接收正文放在body,但是也要考虑当前缓冲区中的数据，是否是全部的正文
        //  3.1缓冲区中的数据，包含了当前请求的所有正文，则取出所需的数据
        if (buf->ReadAbleSize() >= real_len)
        {
            // 只消费当前正文所需的字节，保留缓冲区中可能存在的下一请求数据。
            _request._body.append(buf->ReadPosition(), real_len);
            buf->MoveReadOffset(real_len);
            _recv_statu = RECV_HTTP_OVER;
            return true;
        }
        //  3.2缓冲区中的数据，无法满足当前正文的需要 ，数据不足，取出数据，然后等待新数据到来
        _request._body.append(buf->ReadPosition(), buf->ReadAbleSize());
        buf->MoveReadOffset(buf->ReadAbleSize());
        return true;
    }

public:
    // 初始化为请求行阶段，默认解析结果对应 200 状态。
    HttpContext() : _resp_statu(200), _recv_statu(RECV_HTTP_LINE) {}
    // 处理完当前请求后重置上下文，用于同一 TCP 连接上的下一请求。
    void ReSet()
    {
        _resp_statu = 200;
        _recv_statu = RECV_HTTP_LINE;
        _request.ReSet();
    }
    // 获取解析结果对应的响应状态码。
    int RespStatu() { return _resp_statu; }
    // 获取当前解析阶段，用于区分等待数据、接收完成和解析失败。
    HttpRecVStatu RecvStatu() { return _recv_statu; }
    // 获取请求对象的可修改引用，供业务处理使用。
    HttpRequest &Request() { return _request; }
    // 使用当前缓冲区尽量推进解析；ERROR/OVER 状态下不再消费数据。
    void RecvHttpRequest(Buffer *buf)
    {
        // 有意不写 break：同一批数据可能同时包含请求行、头字段和正文。
        // 每个阶段先校验 _recv_statu；上一步未完成或失败时，后续阶段不会解析。
        switch (_recv_statu)
        {
        case RECV_HTTP_LINE:
            RecvHttpLine(buf);
        case RECV_HTTP_HEAD:
            RecvHttpHead(buf);
        case RECV_HTTP_BODY:
            RecvHttpBody(buf);
        }
        return;
    }
};

class HttpServer
{
private:
    using Handler = std::function<void(const HttpRequest &, HttpResponse *)>;
    using Handlers = std::vector<std::pair<std::regex, Handler>>;
    Handlers _get_route;
    Handlers _post_route;
    Handlers _put_route;
    Handlers _delete_route;
    std::string _basedir; // 静态资源根目录
    TcpServer _server;

private:
    void ErrorHandler(const HttpRequest &req, HttpResponse *rsp)
    {
        // 1. 组织一个错误展示页面
        std::string body;
        body += "<html>";
        body += "<head>";
        body += "<meta http-equiv='Content-Type' content='text/html;charset=utf-8'>";
        body += "</head>";
        body += "<body>";
        body += "<h1>";
        body += std::to_string(rsp->_statu);
        body += " ";
        body += Util::StatuDesc(rsp->_statu);
        body += "</h1>";
        body += "</body>";
        body += "</html>";
        // 2. 将页面数据，当作响应正文，放入rsp中
        rsp->SetContent(body, "text/html");
    }
    // 将HttpResponse中的要素按照http协议格式进行组织，发送
    void WriteReponse(const PtrConnection &conn, const HttpRequest &req, HttpResponse &rsp)
    {
        // 1.先完善头部字段
        if (rsp.HasHeader("Connection") == false && req.Close() == true)
        {
            rsp.SetHeader("Connection", "close");
        }
        else if (rsp.HasHeader("Connection") == false)
        {
            rsp.SetHeader("Connection", "keep-alive");
        }
        if (rsp.HasHeader("Content-Length") == false)
        {
            rsp.SetHeader("Content-Length", std::to_string(rsp._body.size()));
        }
        if (rsp._body.empty() == false && rsp.HasHeader("Content-Type") == false)
        {
            rsp.SetHeader("Content-Type", "application/octet-stream");
        }
        if (rsp._redirect_flag == true)
        {
            rsp.SetHeader("Location", rsp._redirect_url);
        }
        // 2.将rsp中的要素，按照http协议格式进行组织
        std::stringstream rsp_str;
        rsp_str << req._version << " " << std::to_string(rsp._statu) << " " << Util::StatuDesc(rsp._statu) << "\r\n";
        for (auto &head : rsp._headers)
        {
            rsp_str << head.first << ": " << head.second << "\r\n";
        }
        rsp_str << "\r\n";
        if (req._method != "HEAD")
            rsp_str << rsp._body;

        // 3.发送数据
        conn->Send(rsp_str.str().c_str(), rsp_str.str().size());
    }
    // 判断请求是否为静态请求
    bool IsFileHandler(HttpRequest &req)
    {
        // 1.必须设置了静态资源根目录
        if (_basedir.empty())
            return false;
        // 2.请求方法必须是GET / HEAD 请求方法
        if (req._method != "GET" && req._method != "HEAD")
            return false;
        // 3.请求的资源路径必须为一个合法的路径
        if (Util::ValidPath(req._path) == false)
        {
            return false;
        }
        // 4.请求的资源必须存在
        // 有一种请求比较特殊 纯粹的相对根目录： / ，这种情况给后面某人追加一个 index.html
        std::string req_path = _basedir + req._path; // 为了避免直接修改请求的资源路径，因此定义一个临时对象
        if (req._path.back() == '/')
        {
            req_path += "index.html";
        }
        if (Util::IsRegular(req_path) == false)
        {
            return false;
        }
        req._path = req_path;
        return true;
    }
    // 静态资源的请求 -----将静态资源的数据读取出来，放在rsp的_body,并设置mime
    bool FileHandler(const HttpRequest &req, HttpResponse *rsp)
    {
        bool ret = Util::ReadFile(req._path, rsp->_body);
        if (ret == false)
        {
            return false;
        }
        std::string mime = Util::ExtMime(req._path);
        rsp->SetHeader("Content-Type", mime);
        return true;
    }
    // 功能性请求分类处理
    void Dispatcher(HttpRequest &req, HttpResponse *rsp, Handlers &handlers)
    {
        // 在对应请求方法的路由表中，查找是否含有对应资源请求的处理函数，有则调用，无则发送404
        // 思想：路由表存储的是键值对 --正则表达式&处理函数
        // 使用正则表达式，对请求进的路径进行正则匹配，匹配成功就使用对应函数进行处理
        //   /numbers/\d+     /numbers/12345
        for (auto &handler : handlers)
        {
            std::regex &re = handler.first;
            Handler &functor = handler.second;
            bool ret = std::regex_match(req._path, req._matches, re);
            if (ret == false)
            {
                continue;
            }
            functor(req, rsp); // 传入请求信息和空的
            return;
        }
        rsp->_statu = 404;
    }
    void Route(HttpRequest &req, HttpResponse *rsp) // 路由匹配
    {
        // 1.对请求进行分辨，是一个静态资源还是一个功能性请求
        // 静态资源请求，则进行静态资源的处理
        // 功能性请求，则需要通过几个请求路由表来确定是否有处理函数
        //  既不是静态资源请求，也没有设置对应的功能性请求处理函数，就返回404
        if (IsFileHandler(req) == true)
        {
            // 是一个静态资源请求
            FileHandler(req, rsp);
            return;
        }
        if (req._method == "GET" || req._method == "HEAD")
        {
            return Dispatcher(req, rsp, _get_route);
        }
        else if (req._method == "POST")
        {
            return Dispatcher(req, rsp, _post_route);
        }
        else if (req._method == "PUT")
        {
            return Dispatcher(req, rsp, _put_route);
        }
        else if (req._method == "DELETE")
        {
            return Dispatcher(req, rsp, _delete_route);
        }
        rsp->_statu = 405; // NOT FOUNT
        return;
    }
    // 设置上下文
    void OnConnected(const PtrConnection &conn)
    {
        conn->SetContext(HttpContext());
        //DUALLOG_ERROR("NEW CONNECTION %p", conn.get());
    }
    void OnMessage(const PtrConnection &conn, Buffer *buffer) // 缓冲区数据解析+处理
    {
        while (buffer->ReadAbleSize() > 0)
        {
            // 1.获取上下文
            HttpContext *context = conn->GetContext()->get<HttpContext>();
            // 2.通过上下文对缓冲区数据进行解析，得到HttpRequest对象
            //  .1.如果缓冲区的数据解析出错，就直接回复出错响应
            //  .2.如果解析正常且请求已经获取完毕，才开始去进行处理
            context->RecvHttpRequest(buffer);
            HttpResponse rsp(context->RespStatu());

            HttpRequest &req = context->Request();
            if (context->RespStatu() >= 400)
            {
                // 进行错误响应，关闭连接
                rsp.SetHeader("Connection", "close");
                ErrorHandler(req, &rsp);      // 填充一个错误显示页面数据到rsp
                WriteReponse(conn, req, rsp); // 组织响应发送给客户端
                conn->Shutdown();             // 关闭连接
                return;
            }
            if (context->RecvStatu() != RECV_HTTP_OVER)
            {
                // 当前请求还没有接收完整,则退出，等新数据到来再继续处理
                return;
            }

            // 3.请求路由+业务处理
            Route(req, &rsp);
            // 4.对HttpResponse进行组织发送
            WriteReponse(conn, req, rsp);
            // 5.重置上下文
            context->ReSet();
            // 6.根据长短连接判断是否关闭或者继续处理
            if (rsp.Close() == true)
            {
                conn->Shutdown(); // 短连接直接关闭
                return;
            }
        }
    }

public:
    HttpServer(int port, int timeout = DEFALT_TIMEOUT) : _server(port)
    {
        _server.EnableInactiveRelease(timeout);
        _server.SetConnectionCallback(std::bind(&HttpServer::OnConnected, this, std::placeholders::_1));
        _server.SetMessageCallback(std::bind(&HttpServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    }
    // 设置静态资源根目录，用于查找HTML、图片等文件
    void SetBaseDir(const std::string &path)
    {
        int ret = Util::IsDirectory(path) == false;
        assert(ret != true);
        _basedir = path;
    }
    // 设置/添加(请求的正则表达式)与处理函数的映射关系
    //  注册GET请求路由及对应的处理函数
    void Get(const std::string &pattern, const Handler &handler)
    {
        _get_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    // 注册POST请求路由及对应的处理函数
    void Post(const std::string &pattern, const Handler &handler)
    {

        _post_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    // 注册PUT请求路由及对应的处理函数
    void Put(const std::string &pattern, const Handler &handler)
    {
        _put_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    // 注册DELETE请求路由及对应的处理函数
    void Delete(const std::string &pattern, const Handler &handler)
    {
        _delete_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    // 设置从属EventLoop工作线程数量
    void SetThreadCount(int count)
    {

        _server.SetThreadCount(count);
    }

    // 启动服务器事件循环，开始处理客户端连接和HTTP请求
    void Listen()
    {
        _server.start();
    }
};
