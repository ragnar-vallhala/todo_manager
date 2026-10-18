#include "Files.h"
#include "RoomHub.h"
#include "TodoStore.h"

#include <drogon/drogon.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

static void usage(const char *prog)
{
    std::cout << "Usage: " << prog << " [options]\n"
              << "  -H, --host HOST     bind address (default 127.0.0.1)\n"
              << "  -p, --port PORT     port (default 27183)\n"
              << "  -d, --data FILE     data file (default todos.json); attachments go in\n"
              << "                      files/ next to it\n"
              << "  -w, --web DIR       web UI directory (default " TODO_PUBLIC_DIR ")\n";
}

// Room for the biggest file (a 50 MB video) base64-encoded in JSON, or
// wrapped in multipart.
constexpr size_t kMaxBody = files::kMaxDocumentSize + 1024 * 1024;

int main(int argc, char *argv[])
{
    std::string host = "127.0.0.1";
    uint16_t port = 27183;
    std::string dataFile = "todos.json";
    std::string webDir = TODO_PUBLIC_DIR;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value for " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-H" || a == "--host")
            host = next();
        else if (a == "-p" || a == "--port")
            port = static_cast<uint16_t>(std::stoi(next()));
        else if (a == "-d" || a == "--data")
            dataFile = next();
        else if (a == "-w" || a == "--web")
            webDir = next();
        else if (a == "-h" || a == "--help")
            return usage(argv[0]), 0;
        else
            return usage(argv[0]), 2;
    }

    try
    {
        TodoStore::init(dataFile);
        // Attachments live next to the data file.
        files::init((std::filesystem::path(dataFile).parent_path() / "files").string());
        TodoStore::get().setMessageListener([](int64_t roomId) { RoomHub::get().notify(roomId); });
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << "\n";
        return 1;
    }

    std::cout << "Todo server on http://" << host << ":" << port
              << "  (data: " << dataFile << ")\n";

    drogon::app()
        .setLogLevel(trantor::Logger::kWarn)
        .addListener(host, port)
        .setThreadNum(2)
        // Room reads may wait up to 300 s for news; the default (60 s) would
        // drop them first.
        .setIdleConnectionTimeout(330)
        // Uploads are up to 50 MB (videos; Files.h); over MCP they come
        // base64-encoded, a third bigger. Bodies are kept in memory rather
        // than in temp files.
        .setClientMaxBodySize(kMaxBody)
        .setClientMaxMemoryBodySize(kMaxBody)
        // The only fractions are video lengths, in seconds: milliseconds will do.
        .setFloatPrecisionInJson(3, "decimal")
        .setDocumentRoot(webDir)
        .run();
}
