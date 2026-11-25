#include "mcp_server.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>

// ---------------------------------------------------------------------------
// Platform socket layer
// ---------------------------------------------------------------------------
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_handle = SOCKET;
static const socket_handle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_handle = int;
static const socket_handle kInvalidSocket = -1;
#endif

namespace
{
    constexpr size_t kMaxHeaderBytes = 128 * 1024;
    constexpr size_t kMaxBodyBytes = 4 * 1024 * 1024;
    constexpr int kRequestTimeoutSeconds = 5;
    constexpr int kCommandTimeoutSeconds = 10;

    void closeSocket(socket_handle s)
    {
        if (s == kInvalidSocket)
            return;
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
    }

    int lastSocketError()
    {
#ifdef _WIN32
        return WSAGetLastError();
#else
        return errno;
#endif
    }

    std::string trim(const std::string &value)
    {
        size_t start = 0;
        while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
            ++start;
        size_t end = value.size();
        while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
            --end;
        return value.substr(start, end - start);
    }

    std::string toLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool sendAll(socket_handle s, const std::string &data)
    {
        size_t sent = 0;
        while (sent < data.size())
        {
#ifdef MSG_NOSIGNAL
            const int flags = MSG_NOSIGNAL;
#else
            const int flags = 0;
#endif
            int n = ::send(s, data.data() + sent,
                           static_cast<int>(data.size() - sent), flags);
            if (n <= 0)
                return false;
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    const char *statusText(int status)
    {
        switch (status)
        {
        case 200: return "OK";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        default:  return "Internal Server Error";
        }
    }

    using HeaderList = std::vector<std::pair<std::string, std::string>>;

    bool sendHttp(socket_handle client, int status, const std::string &contentType,
                  const std::string &body, const HeaderList &extra = {},
                  const std::string &sessionId = "")
    {
        std::ostringstream head;
        head << "HTTP/1.1 " << status << " " << statusText(status) << "\r\n";
        if (!contentType.empty())
            head << "Content-Type: " << contentType << "\r\n";
        head << "Content-Length: " << body.size() << "\r\n";
        head << "Connection: close\r\n";
        head << "Access-Control-Allow-Origin: *\r\n";
        head << "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n";
        head << "Access-Control-Allow-Headers: Content-Type, Authorization, Mcp-Session-Id, Mcp-Protocol-Version\r\n";
        head << "Access-Control-Expose-Headers: Mcp-Session-Id\r\n";
        if (!sessionId.empty())
            head << "Mcp-Session-Id: " << sessionId << "\r\n";
        for (const auto &header : extra)
            head << header.first << ": " << header.second << "\r\n";
        head << "\r\n";

        return sendAll(client, head.str()) && sendAll(client, body);
    }

    struct HttpRequest
    {
        std::string method;
        std::string path;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string body;
    };

    bool receiveSome(socket_handle client, std::string &buffer, std::string &error)
    {
        char chunk[8192];
        int n = ::recv(client, chunk, static_cast<int>(sizeof(chunk)), 0);
        if (n <= 0)
        {
            error = "connection closed while reading request";
            return false;
        }
        buffer.append(chunk, static_cast<size_t>(n));
        return true;
    }

    /**
     * @brief Decodes a chunked body from the bytes buffered so far.
     * @return true when a full body could be decoded, false when more bytes are needed.
     */
    bool decodeChunked(const std::string &raw, std::string &out, bool &complete)
    {
        out.clear();
        complete = false;
        size_t pos = 0;
        while (true)
        {
            size_t lineEnd = raw.find("\r\n", pos);
            if (lineEnd == std::string::npos)
                return false;

            std::string sizeLine = raw.substr(pos, lineEnd - pos);
            size_t extension = sizeLine.find(';');
            if (extension != std::string::npos)
                sizeLine = sizeLine.substr(0, extension);
            sizeLine = trim(sizeLine);
            if (sizeLine.empty())
                return false;

            char *endPtr = nullptr;
            unsigned long chunkSize = std::strtoul(sizeLine.c_str(), &endPtr, 16);
            if (endPtr == sizeLine.c_str())
                return false;

            if (chunkSize == 0)
            {
                // The terminating chunk is followed by optional trailers and an
                // empty line. Everything up to that point has been consumed.
                complete = true;
                return true;
            }

            const size_t dataStart = lineEnd + 2;
            if (raw.size() < dataStart + chunkSize + 2)
                return false;
            out.append(raw, dataStart, chunkSize);
            pos = dataStart + chunkSize + 2;
        }
    }

    bool readHttpRequest(socket_handle client, HttpRequest &request, std::string &error)
    {
        std::string buffer;
        size_t headerEnd = std::string::npos;
        while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos)
        {
            if (buffer.size() > kMaxHeaderBytes)
            {
                error = "request headers too large";
                return false;
            }
            if (!receiveSome(client, buffer, error))
                return false;
        }

        const std::string headerBlock = buffer.substr(0, headerEnd);
        std::string body = buffer.substr(headerEnd + 4);

        std::istringstream headerStream(headerBlock);
        std::string line;
        bool firstLine = true;
        while (std::getline(headerStream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (firstLine)
            {
                firstLine = false;
                std::istringstream requestLine(line);
                requestLine >> request.method >> request.path >> request.version;
                if (request.method.empty() || request.path.empty())
                {
                    error = "malformed HTTP request line";
                    return false;
                }
                continue;
            }

            const size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            const std::string name = toLower(trim(line.substr(0, colon)));
            const std::string value = trim(line.substr(colon + 1));
            request.headers[name] = value;
        }

        auto headerValue = [&request](const std::string &name) -> std::string
        {
            auto it = request.headers.find(name);
            return it == request.headers.end() ? std::string() : it->second;
        };

        // Some HTTP clients send "Expect: 100-continue" and wait for the
        // interim response before transmitting the body.
        if (toLower(headerValue("expect")).find("100-continue") != std::string::npos)
            sendAll(client, "HTTP/1.1 100 Continue\r\n\r\n");

        const bool chunked = toLower(headerValue("transfer-encoding")).find("chunked") != std::string::npos;
        if (chunked)
        {
            while (true)
            {
                bool complete = false;
                if (decodeChunked(body, request.body, complete))
                    break;
                if (body.size() > kMaxBodyBytes)
                {
                    error = "request body too large";
                    return false;
                }
                if (!receiveSome(client, body, error))
                    return false;
            }
        }
        else
        {
            size_t contentLength = 0;
            const std::string lengthHeader = headerValue("content-length");
            if (!lengthHeader.empty())
            {
                try
                {
                    long long parsed = std::stoll(lengthHeader);
                    if (parsed < 0)
                        parsed = 0;
                    contentLength = static_cast<size_t>(parsed);
                }
                catch (...)
                {
                    contentLength = 0;
                }
            }

            if (contentLength > kMaxBodyBytes)
            {
                error = "request body too large";
                return false;
            }

            while (body.size() < contentLength)
            {
                if (!receiveSome(client, body, error))
                    return false;
            }
            body.resize(contentLength);
            request.body = body;
        }

        return true;
    }

    std::string requestPathWithoutQuery(const std::string &rawPath)
    {
        std::string path = rawPath;
        // Absolute-form request targets (http://host:port/mcp) are legal HTTP.
        const size_t scheme = path.find("://");
        if (scheme != std::string::npos)
        {
            const size_t slash = path.find('/', scheme + 3);
            path = (slash == std::string::npos) ? "/" : path.substr(slash);
        }
        const size_t query = path.find('?');
        if (query != std::string::npos)
            path = path.substr(0, query);
        return path;
    }

    std::string normalizeEndpoint(std::string endpoint)
    {
        endpoint = trim(endpoint);
        if (endpoint.empty())
            endpoint = "/mcp";
        if (endpoint.front() != '/')
            endpoint.insert(endpoint.begin(), '/');
        while (endpoint.size() > 1 && endpoint.back() == '/')
            endpoint.pop_back();
        return endpoint;
    }

    std::string makeSessionId()
    {
        static std::atomic<unsigned long long> counter{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::ostringstream out;
        out << "kemena-" << std::hex << static_cast<unsigned long long>(now)
            << "-" << counter.fetch_add(1);
        return out.str();
    }

    bool resolveIpv4(const std::string &host, in_addr &address)
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo *result = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr)
            return false;
        address = reinterpret_cast<sockaddr_in *>(result->ai_addr)->sin_addr;
        freeaddrinfo(result);
        return true;
    }

    nlohmann::json rpcResult(const nlohmann::json &id, const nlohmann::json &result)
    {
        return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
    }

    nlohmann::json rpcError(const nlohmann::json &id, int code,
                            const std::string &message, const nlohmann::json &data = nullptr)
    {
        nlohmann::json error = {{"code", code}, {"message", message}};
        if (!data.is_null())
            error["data"] = data;
        return {{"jsonrpc", "2.0"}, {"id", id}, {"error", error}};
    }

    nlohmann::json toolSuccess(const nlohmann::json &structured)
    {
        nlohmann::json content = nlohmann::json::array();
        content.push_back({{"type", "text"}, {"text", structured.dump(2)}});
        return {{"content", content},
                {"structuredContent", structured},
                {"isError", false}};
    }

    nlohmann::json toolFailure(const std::string &message)
    {
        nlohmann::json content = nlohmann::json::array();
        content.push_back({{"type", "text"}, {"text", message}});
        return {{"content", content}, {"isError", true}};
    }

    std::string localTimestamp()
    {
        std::time_t now = std::time(nullptr);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char buffer[32] = {};
        std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
        return buffer;
    }
}

// ---------------------------------------------------------------------------
// McpServer
// ---------------------------------------------------------------------------
McpServer::McpServer()
{
    statusSnapshot = nlohmann::json::object();
}

McpServer::~McpServer()
{
    stop();
}

bool McpServer::start(const McpServerConfig &newConfig)
{
    if (worker.joinable())
        stop();

    if (newConfig.port < 1 || newConfig.port > 65535)
    {
        setState(McpServerState::Error, "Port must be between 1 and 65535.");
        log("Start failed: invalid port " + std::to_string(newConfig.port));
        return false;
    }
    if (trim(newConfig.host).empty())
    {
        setState(McpServerState::Error, "Host cannot be empty.");
        log("Start failed: empty host");
        return false;
    }

    config = newConfig;
    config.endpoint = normalizeEndpoint(config.endpoint);
    if (config.serverName.empty())
        config.serverName = "Kemena3D Editor";
    if (config.serverVersion.empty())
        config.serverVersion = "1.0.0";

    stopRequested = false;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastError.clear();
        state = McpServerState::Starting;
    }

    log("Starting MCP server on " + getListenUrl());
    worker = std::thread(&McpServer::run, this);
    return true;
}

void McpServer::stop()
{
    stopRequested = true;
    queueCv.notify_all();

    if (worker.joinable())
        worker.join();

    running = false;
    if (getState() != McpServerState::Error)
        setState(McpServerState::Stopped);
}

void McpServer::run()
{
    std::string startupError;

#ifdef _WIN32
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        startupError = "WSAStartup failed.";
        setState(McpServerState::Error, startupError);
        log("Start failed: " + startupError);
        return;
    }
#endif

    socket_handle listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == kInvalidSocket)
    {
        startupError = "Could not create listening socket (error " +
                       std::to_string(lastSocketError()) + ").";
        setState(McpServerState::Error, startupError);
        log("Start failed: " + startupError);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    int reuse = 1;
    ::setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(config.port));
    if (!resolveIpv4(config.host, address.sin_addr))
    {
        startupError = "Could not resolve host '" + config.host + "'.";
        setState(McpServerState::Error, startupError);
        log("Start failed: " + startupError);
        closeSocket(listenSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (::bind(listenSocket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
    {
        startupError = "Could not bind to " + config.host + ":" +
                       std::to_string(config.port) + " (error " +
                       std::to_string(lastSocketError()) + ").";
        setState(McpServerState::Error, startupError);
        log("Start failed: " + startupError);
        closeSocket(listenSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    if (::listen(listenSocket, SOMAXCONN) != 0)
    {
        startupError = "Could not listen on port " + std::to_string(config.port) +
                       " (error " + std::to_string(lastSocketError()) + ").";
        setState(McpServerState::Error, startupError);
        log("Start failed: " + startupError);
        closeSocket(listenSocket);
#ifdef _WIN32
        WSACleanup();
#endif
        return;
    }

    running = true;
    setState(McpServerState::Running);
    log("Listening on " + getListenUrl());

    while (!stopRequested.load())
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;

        const int ready = ::select(static_cast<int>(listenSocket + 1), &readSet,
                                   nullptr, nullptr, &timeout);
        if (stopRequested.load())
            break;
        if (ready <= 0)
            continue;
        if (!FD_ISSET(listenSocket, &readSet))
            continue;

        sockaddr_in clientAddress{};
#ifdef _WIN32
        int clientLength = sizeof(clientAddress);
#else
        socklen_t clientLength = sizeof(clientAddress);
#endif
        socket_handle client = ::accept(listenSocket,
                                        reinterpret_cast<sockaddr *>(&clientAddress),
                                        &clientLength);
        if (client == kInvalidSocket)
            continue;

#ifdef _WIN32
        DWORD recvTimeout = kRequestTimeoutSeconds * 1000;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&recvTimeout), sizeof(recvTimeout));
        DWORD sendTimeout = kRequestTimeoutSeconds * 1000;
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char *>(&sendTimeout), sizeof(sendTimeout));
#else
        timeval ioTimeout{};
        ioTimeout.tv_sec = kRequestTimeoutSeconds;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &ioTimeout, sizeof(ioTimeout));
        ::setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &ioTimeout, sizeof(ioTimeout));
#endif

        char remoteBuffer[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &clientAddress.sin_addr, remoteBuffer, sizeof(remoteBuffer));
        const std::string remote = std::string(remoteBuffer) + ":" +
                                   std::to_string(ntohs(clientAddress.sin_port));

        HttpRequest request;
        std::string readError;
        if (!readHttpRequest(client, request, readError))
        {
            if (readError.find("too large") != std::string::npos)
                sendHttp(client, 413, "text/plain; charset=utf-8", readError);
            closeSocket(client);
            continue;
        }

        const std::string path = requestPathWithoutQuery(request.path);
        const std::string method = toLower(request.method);

        if (method == "options")
        {
            sendHttp(client, 204, "", "");
            closeSocket(client);
            continue;
        }

        if (method == "get" && (path == "/" || path == "/health"))
        {
            nlohmann::json health = {
                {"status", "ok"},
                {"server", config.serverName},
                {"version", config.serverVersion},
                {"endpoint", config.endpoint},
                {"requests", requestCount.load()}};
            sendHttp(client, 200, "application/json; charset=utf-8", health.dump());
            closeSocket(client);
            continue;
        }

        if (path != config.endpoint)
        {
            nlohmann::json body = {
                {"error", "not_found"},
                {"message", "MCP endpoint is " + config.endpoint}};
            sendHttp(client, 404, "application/json; charset=utf-8", body.dump());
            closeSocket(client);
            continue;
        }

        if (method != "post" && method != "delete")
        {
            sendHttp(client, 405, "application/json; charset=utf-8",
                     R"({"error":"method_not_allowed","message":"Use POST for MCP JSON-RPC requests."})",
                     {{"Allow", "POST, DELETE, OPTIONS"}});
            closeSocket(client);
            continue;
        }

        if (config.authEnabled)
        {
            auto authIt = request.headers.find("authorization");
            const std::string expected = "Bearer " + config.authToken;
            const bool authorized = authIt != request.headers.end() &&
                                    !config.authToken.empty() &&
                                    authIt->second == expected;
            if (!authorized)
            {
                nlohmann::json error = rpcError(nullptr, -32001, "Unauthorized");
                sendHttp(client, 401, "application/json; charset=utf-8", error.dump(),
                         {{"WWW-Authenticate", "Bearer"}});
                log("Rejected unauthorized request from " + remote);
                closeSocket(client);
                continue;
            }
        }

        if (method == "delete")
        {
            sendHttp(client, 200, "application/json; charset=utf-8", "{}");
            log("Session termination requested by " + remote);
            closeSocket(client);
            continue;
        }

        requestCount.fetch_add(1);

        nlohmann::json payload = nlohmann::json::parse(request.body, nullptr, false);
        if (payload.is_discarded())
        {
            nlohmann::json error = rpcError(nullptr, -32700, "Parse error");
            sendHttp(client, 400, "application/json; charset=utf-8", error.dump());
            log("Rejected malformed JSON from " + remote);
            closeSocket(client);
            continue;
        }

        std::string sessionId;
        nlohmann::json responsePayload;
        bool sendResponse = false;

        if (payload.is_array())
        {
            responsePayload = nlohmann::json::array();
            if (payload.empty())
            {
                nlohmann::json error = rpcError(nullptr, -32600, "Invalid Request");
                sendHttp(client, 400, "application/json; charset=utf-8", error.dump());
                closeSocket(client);
                continue;
            }
            for (const auto &entry : payload)
            {
                bool entryHasResponse = true;
                nlohmann::json entryResponse = processRpc(entry, entryHasResponse, sessionId);
                if (entryHasResponse)
                {
                    responsePayload.push_back(entryResponse);
                    sendResponse = true;
                }
            }
        }
        else
        {
            responsePayload = processRpc(payload, sendResponse, sessionId);
        }

        if (!sendResponse)
        {
            // Notifications only: acknowledge without a JSON-RPC body.
            sendHttp(client, 202, "", "", {}, sessionId);
        }
        else
        {
            sendHttp(client, 200, "application/json; charset=utf-8",
                     responsePayload.dump(), {}, sessionId);
        }

        log("Handled " + method + " request from " + remote);
        closeSocket(client);
    }

    running = false;
    closeSocket(listenSocket);
#ifdef _WIN32
    WSACleanup();
#endif
    if (getState() != McpServerState::Error)
        setState(McpServerState::Stopped);
    log("MCP server stopped");
}

nlohmann::json McpServer::processRpc(const nlohmann::json &request, bool &hasResponse,
                                     std::string &sessionId)
{
    hasResponse = true;

    if (!request.is_object())
        return rpcError(nullptr, -32600, "Invalid Request");

    const bool isNotification = !request.contains("id");
    nlohmann::json id = request.contains("id") ? request["id"] : nlohmann::json();

    if (isNotification)
    {
        // MCP notifications (e.g. notifications/initialized) never receive a
        // JSON-RPC response. They are acknowledged with HTTP 202 by the caller.
        hasResponse = false;
        return nlohmann::json();
    }

    const std::string method = request.value("method", "");
    const nlohmann::json params = request.value("params", nlohmann::json::object());

    if (method == "initialize")
    {
        const std::string requested = params.value("protocolVersion", "2025-06-18");
        const std::vector<std::string> supported = {
            "2025-06-18", "2025-03-26", "2024-11-05"};
        const std::string negotiated =
            std::find(supported.begin(), supported.end(), requested) != supported.end()
                ? requested
                : supported.front();

        sessionId = makeSessionId();

        nlohmann::json result = {
            {"protocolVersion", negotiated},
            {"capabilities", {
                {"tools", {{"listChanged", false}}},
                {"resources", {{"subscribe", false}, {"listChanged", false}}},
                {"prompts", {{"listChanged", false}}},
                {"logging", nlohmann::json::object()}}},
            {"serverInfo", {
                {"name", config.serverName},
                {"version", config.serverVersion}}},
            {"instructions",
             "Kemena3D editor MCP server. Use the kemena_* tools to inspect and "
             "control the running editor."}};

        return rpcResult(id, result);
    }

    if (method == "ping")
        return rpcResult(id, nlohmann::json::object());

    if (method == "tools/list")
        return rpcResult(id, {{"tools", buildToolList()}});

    if (method == "tools/call")
    {
        const std::string name = params.value("name", "");
        nlohmann::json arguments = params.value("arguments", nlohmann::json::object());
        if (!arguments.is_object())
            arguments = nlohmann::json::object();
        if (name.empty())
            return rpcError(id, -32602, "Missing tool name");

        return rpcResult(id, invokeTool(name, arguments));
    }

    if (method == "resources/list")
        return rpcResult(id, {{"resources", nlohmann::json::array()}});

    if (method == "resources/templates/list")
        return rpcResult(id, {{"resourceTemplates", nlohmann::json::array()}});

    if (method == "prompts/list")
        return rpcResult(id, {{"prompts", nlohmann::json::array()}});

    if (method == "logging/setLevel")
        return rpcResult(id, nlohmann::json::object());

    return rpcError(id, -32601, "Method not found: " + method);
}

nlohmann::json McpServer::invokeTool(const std::string &name, const nlohmann::json &args)
{
    log("tools/call " + name);

    if (name == "kemena_editor_status")
        return toolSuccess(getStatusSnapshot());

    if (name == "kemena_list_panels")
    {
        nlohmann::json snapshot = getStatusSnapshot();
        if (snapshot.contains("panels"))
            return toolSuccess(snapshot["panels"]);
        return toolSuccess(nlohmann::json::object());
    }

    static const std::vector<std::string> queuedTools = {
        "kemena_list_scene_objects",
        "kemena_set_panel_visibility",
        "kemena_console_log",
        "kemena_select_object"};

    if (std::find(queuedTools.begin(), queuedTools.end(), name) != queuedTools.end())
    {
        nlohmann::json result;
        std::string error;
        if (!executeQueued(name, args, result, error))
            return toolFailure(error);
        if (!error.empty())
            return toolFailure(error);
        return toolSuccess(result);
    }

    return toolFailure("Unknown tool: " + name);
}

bool McpServer::executeQueued(const std::string &name, const nlohmann::json &args,
                              nlohmann::json &result, std::string &error)
{
    McpCommand command;
    command.id = nextCommandId.fetch_add(1);
    command.tool = name;
    command.arguments = args;

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pendingCommands.push_back(command);
    }

    std::unique_lock<std::mutex> lock(queueMutex);
    const bool finished = queueCv.wait_for(
        lock, std::chrono::seconds(kCommandTimeoutSeconds),
        [this, &command]()
        {
            return completedCommands.count(command.id) > 0 || stopRequested.load();
        });

    if (!finished)
    {
        // The editor never drained the command (e.g. the UI thread is blocked).
        for (auto it = pendingCommands.begin(); it != pendingCommands.end(); ++it)
        {
            if (it->id == command.id)
            {
                pendingCommands.erase(it);
                break;
            }
        }
        error = "Editor did not respond in time. Is the editor UI blocked?";
        return false;
    }

    auto completedIt = completedCommands.find(command.id);
    if (completedIt == completedCommands.end())
    {
        error = stopRequested.load() ? "Server stopped before the tool completed."
                                     : "Tool result was lost.";
        return false;
    }

    if (!completedIt->second.ok)
        error = completedIt->second.error;
    else
        result = completedIt->second.result;

    completedCommands.erase(completedIt);
    return true;
}

void McpServer::setState(McpServerState newState, const std::string &error)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    state = newState;
    lastError = error;
}

McpServerState McpServer::getState() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return state;
}

std::string McpServer::getStateString() const
{
    switch (getState())
    {
    case McpServerState::Running:  return "Running";
    case McpServerState::Starting: return "Starting";
    case McpServerState::Error:    return "Error";
    case McpServerState::Stopped:
    default:                       return "Stopped";
    }
}

std::string McpServer::getLastError() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return lastError;
}

std::string McpServer::getListenUrl() const
{
    std::string host = config.host;
    if (host.empty())
        host = "127.0.0.1";
    if (host == "0.0.0.0")
        host = "127.0.0.1";
    return "http://" + host + ":" + std::to_string(config.port) +
           normalizeEndpoint(config.endpoint);
}

void McpServer::setStatusSnapshot(const nlohmann::json &snapshot)
{
    std::lock_guard<std::mutex> lock(snapshotMutex);
    statusSnapshot = snapshot;
}

nlohmann::json McpServer::getStatusSnapshot() const
{
    std::lock_guard<std::mutex> lock(snapshotMutex);
    return statusSnapshot;
}

bool McpServer::pollCommand(McpCommand &out)
{
    std::lock_guard<std::mutex> lock(queueMutex);
    if (pendingCommands.empty())
        return false;
    out = pendingCommands.front();
    pendingCommands.pop_front();
    return true;
}

void McpServer::completeCommand(uint64_t id, bool ok, const nlohmann::json &result,
                                const std::string &error)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        completedCommands[id] = CommandResult{ok, result, error};
    }
    queueCv.notify_all();
}

std::vector<std::string> McpServer::getLog() const
{
    std::lock_guard<std::mutex> lock(logMutex);
    return std::vector<std::string>(logLines.begin(), logLines.end());
}

void McpServer::clearLog()
{
    std::lock_guard<std::mutex> lock(logMutex);
    logLines.clear();
}

void McpServer::log(const std::string &line)
{
    std::lock_guard<std::mutex> lock(logMutex);
    logLines.push_back("[" + localTimestamp() + "] " + line);
    while (logLines.size() > 200)
        logLines.pop_front();
}

nlohmann::json McpServer::buildToolList()
{
    nlohmann::json tools = nlohmann::json::array();

    tools.push_back({
        {"name", "kemena_editor_status"},
        {"title", "Editor Status"},
        {"description",
         "Return the current Kemena3D editor status: server endpoint, whether a "
         "project is open, project name/path, scene object count and current "
         "selection."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"additionalProperties", false}}},
        {"annotations", {{"readOnlyHint", true}, {"openWorldHint", false}}}});

    tools.push_back({
        {"name", "kemena_list_panels"},
        {"title", "List Editor Panels"},
        {"description",
         "List the editor's dockable panels and whether each one is currently "
         "visible."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"additionalProperties", false}}},
        {"annotations", {{"readOnlyHint", true}, {"openWorldHint", false}}}});

    tools.push_back({
        {"name", "kemena_list_scene_objects"},
        {"title", "List Scene Objects"},
        {"description",
         "Return the top-level objects in the active scene, including each "
         "object's UUID, display name and node type."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", nlohmann::json::object()},
            {"additionalProperties", false}}},
        {"annotations", {{"readOnlyHint", true}, {"openWorldHint", false}}}});

    tools.push_back({
        {"name", "kemena_set_panel_visibility"},
        {"title", "Set Panel Visibility"},
        {"description",
         "Show or hide one of the editor's dockable panels. This lets an MCP "
         "client open the MCP panel itself or reveal another editor surface."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"panel", {
                    {"type", "string"},
                    {"description", "Panel to show or hide."},
                    {"enum", nlohmann::json::array({
                        "world", "inspector", "hierarchy", "project", "console",
                        "shaderEditor", "scriptEditor", "game", "animatorEditor",
                        "animationEditor", "particleEditor", "guiEditor", "mcp"})}}},
                {"visible", {
                    {"type", "boolean"},
                    {"description", "true to show the panel, false to hide it."}}}}},
            {"required", nlohmann::json::array({"panel", "visible"})},
            {"additionalProperties", false}}},
        {"annotations", {{"readOnlyHint", false}, {"destructiveHint", false},
                         {"openWorldHint", false}}}});

    tools.push_back({
        {"name", "kemena_console_log"},
        {"title", "Write Console Log"},
        {"description",
         "Append a message to the Kemena3D Console panel so editor users can see "
         "what an MCP client is doing."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"message", {
                    {"type", "string"},
                    {"description", "Message to append to the console."}}},
                {"level", {
                    {"type", "string"},
                    {"description", "Severity used to colour the console line."},
                    {"enum", nlohmann::json::array({"info", "warning", "error"})},
                    {"default", "info"}}}}},
            {"required", nlohmann::json::array({"message"})},
            {"additionalProperties", false}}},
        {"annotations", {{"readOnlyHint", false}, {"destructiveHint", false},
                         {"openWorldHint", false}}}});

    tools.push_back({
        {"name", "kemena_select_object"},
        {"title", "Select Scene Object"},
        {"description",
         "Select a scene object by UUID in the Hierarchy/Inspector, optionally "
         "clearing the previous selection."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"uuid", {
                    {"type", "string"},
                    {"description", "UUID of the object to select."}}},
                {"clearSelection", {
                    {"type", "boolean"},
                    {"description", "Replace the current selection instead of adding to it."},
                    {"default", true}}}}},
            {"required", nlohmann::json::array({"uuid"})},
            {"additionalProperties", false}}},
        {"annotations", {{"readOnlyHint", false}, {"destructiveHint", false},
                         {"openWorldHint", false}}}});

    return tools;
}
