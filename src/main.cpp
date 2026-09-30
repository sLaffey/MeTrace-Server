#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include <httplib.h>

#include "service/DataBase.h"

namespace {

constexpr const char* kDefaultHost = "127.0.0.1";
constexpr int kDefaultPort = 8000;
constexpr const char* kDefaultDbPath = "./data/metrace.db";

constexpr int kMinPort = 1;
constexpr int kMaxPort = 65535;

// 启动参数
struct Options {
    std::string host = kDefaultHost;
    int port = kDefaultPort;
    std::string dbPath = kDefaultDbPath;
};

void printUsage(const char* program)
{
    std::cout << "用法: " << program << " [选项]\n"
              << "  --host <addr>  监听地址，默认 " << kDefaultHost << "（环境变量 METRACE_HOST）\n"
              << "  --port <port>  监听端口，默认 " << kDefaultPort << "（环境变量 METRACE_PORT）\n"
              << "  --db <path>    数据存储路径（采用 json 格式），默认 " << kDefaultDbPath
              << "（环境变量 METRACE_DB）\n"
              << "  --help         显示本帮助\n";
}

// 解析 `--key value` 形式的参数。优先级：命令行 > 环境变量 > 默认值。
// 解析失败时打印原因并返回 false。
bool parseArgs(int argc, char** argv, Options& options, bool& showHelp)
{
    std::optional<std::string> host;
    std::optional<int> port;
    std::optional<std::string> dbPath;

    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];

        if (key == "--help" || key == "-h") {
            showHelp = true;
            return true;
        }

        if (i + 1 >= argc) {
            std::cerr << "参数 " << key << " 缺少取值" << std::endl;
            return false;
        }
        const std::string value = argv[++i];

        if (key == "--host") {
            host = value;
        } else if (key == "--port") {
            try {
                const int parsed = std::stoi(value);
                if (parsed < kMinPort || parsed > kMaxPort) {
                    throw std::out_of_range("port out of range");
                }
                port = parsed;
            } catch (const std::exception&) {
                std::cerr << "端口必须是 " << kMinPort << "~" << kMaxPort << " 之间的整数，收到: "
                          << value << std::endl;
                return false;
            }
        } else if (key == "--db") {
            dbPath = value;
        } else {
            std::cerr << "未知参数: " << key << std::endl;
            return false;
        }
    }

    // 命令行没给的值，用环境变量兜底
    if (!host) {
        if (const char* env = std::getenv("METRACE_HOST")) {
            host = env;
        }
    }
    if (!port) {
        if (const char* env = std::getenv("METRACE_PORT")) {
            try {
                port = std::stoi(env);
            } catch (const std::exception&) {
                std::cerr << "环境变量 METRACE_PORT 不是合法整数: " << env << std::endl;
                return false;
            }
        }
    }
    if (!dbPath) {
        if (const char* env = std::getenv("METRACE_DB")) {
            dbPath = env;
        }
    }

    options.host = host.value_or(kDefaultHost);
    options.port = port.value_or(kDefaultPort);
    options.dbPath = dbPath.value_or(kDefaultDbPath);
    return true;
}

// 数据库文件所在目录可能还不存在（默认是 ./data/），需要先建出来。
bool ensureParentDirectory(const std::filesystem::path& dbPath)
{
    const std::filesystem::path parent = dbPath.parent_path();
    if (parent.empty()) {
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        std::cerr << "无法创建数据目录 " << parent << ": " << ec.message() << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    bool showHelp = false;

    if (!parseArgs(argc, argv, options, showHelp)) {
        printUsage(argv[0]);
        return 1;
    }
    if (showHelp) {
        printUsage(argv[0]);
        return 0;
    }

    const std::filesystem::path dbPath(options.dbPath);
    if (!ensureParentDirectory(dbPath)) {
        return 1;
    }

    // 开库。必须在 listen 之前完成，之后就是只读了。
    try {
        metrace::service::DataBase db(dbPath.string());
    } catch (const std::exception& e) {
        std::cerr << "打开数据库失败 '" << dbPath.string() << "': " << e.what() << std::endl;
        return 1;
    }

    httplib::Server server;
    // metrace::http::registerRoutes(server);

    std::cout << "MeTrace-Server 已启动: http://" << options.host << ":" << options.port
              << "  (db: " << dbPath.string() << ")" << std::endl;

    if (!server.listen(options.host, options.port)) {
        std::cerr << "监听 " << options.host << ":" << options.port << " 失败" << std::endl;
        return 1;
    }
    return 0;
}
