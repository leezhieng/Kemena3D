#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/**
 * @brief User-editable settings for the embedded MCP server.
 *
 * The panel persists these values to Config/mcp.json so the server can be
 * brought up with the same endpoint after restarting the editor.
 */
struct McpServerConfig
{
    std::string host = "127.0.0.1";           ///< Interface to bind ("127.0.0.1" = local only, "0.0.0.0" = all).
    int port = 8765;                          ///< TCP port to listen on.
    std::string endpoint = "/mcp";            ///< HTTP path that accepts MCP JSON-RPC traffic.
    std::string serverName = "Kemena3D Editor"; ///< Name advertised in the MCP initialize response.
    std::string serverVersion = "1.0.0";      ///< Version advertised in the MCP initialize response.
    bool authEnabled = false;                 ///< Require an "Authorization: Bearer <token>" header.
    std::string authToken;                    ///< Token expected when @ref authEnabled is true.
};

/** @brief Lifecycle state of the MCP server. */
enum class McpServerState
{
    Stopped,  ///< Not listening.
    Starting, ///< Worker thread is bringing the socket up.
    Running,  ///< Listening and accepting requests.
    Error     ///< The last start attempt failed; see getLastError().
};

/**
 * @brief A tool invocation forwarded from the network thread to the editor UI thread.
 *
 * MCP requests are handled on a background thread, but tools such as
 * "set panel visibility" must mutate editor state on the main thread. The
 * server therefore queues those calls and the MCP panel drains the queue once
 * per frame via pollCommand()/completeCommand().
 */
struct McpCommand
{
    uint64_t id = 0;                 ///< Unique id used to match the completion result.
    std::string tool;                ///< Tool name from tools/call.
    nlohmann::json arguments;        ///< "arguments" object from tools/call (always an object).
};

/**
 * @brief Small, dependency-free MCP (Model Context Protocol) server for the editor.
 *
 * Speaks the Streamable HTTP transport: JSON-RPC 2.0 over HTTP POST using the
 * configured endpoint. The implementation intentionally keeps one request per
 * connection (`Connection: close`) so no session state has to be retained
 * between HTTP requests.
 *
 * Supported MCP methods:
 *  - initialize / notifications/initialized
 *  - ping
 *  - tools/list / tools/call
 *  - resources/list (empty) / prompts/list (empty)
 *
 * The background thread is started and stopped from the MCP panel. All editor
 * mutation tools are marshalled to the main thread through a small command
 * queue, keeping engine/UI access single-threaded.
 */
class McpServer
{
    public:
        /** @brief Constructs an idle server. */
        McpServer();

        /** @brief Stops the server if it is still running. */
        ~McpServer();

        McpServer(const McpServer &) = delete;
        McpServer &operator=(const McpServer &) = delete;

        /**
         * @brief Starts the listener on a background thread.
         * @param config Settings to apply for this run.
         * @return true if the worker thread was launched (listen errors are
         *         reported asynchronously through getState()/getLastError()).
         */
        bool start(const McpServerConfig &config);

        /** @brief Requests shutdown and joins the worker thread. */
        void stop();

        /** @brief Returns true while the listener is accepting connections. */
        bool isRunning() const { return running.load(); }

        /** @brief Returns the current lifecycle state. */
        McpServerState getState() const;

        /** @brief Returns a human-readable state string ("Running", "Error", ...). */
        std::string getStateString() const;

        /** @brief Returns the message from the last failed start, if any. */
        std::string getLastError() const;

        /** @brief Returns the URL clients should connect to (e.g. http://127.0.0.1:8765/mcp). */
        std::string getListenUrl() const;

        /**
         * @brief Publishes a thread-safe snapshot of editor state for read-only tools.
         *
         * Called by the panel once per frame on the main thread. The server
         * copies this value while holding a mutex, so tools can safely read it
         * from the network thread.
         */
        void setStatusSnapshot(const nlohmann::json &snapshot);

        /** @brief Returns the most recently published snapshot. */
        nlohmann::json getStatusSnapshot() const;

        /**
         * @brief Pops the next command queued by a tools/call request.
         * @param out Receives the command when one is available.
         * @return true if a command was returned, false when the queue is empty.
         */
        bool pollCommand(McpCommand &out);

        /**
         * @brief Reports the result of a command previously returned by pollCommand().
         * @param id     Command id being completed.
         * @param ok     true when the tool succeeded.
         * @param result Structured result (JSON) to include in the tool response.
         * @param error  Human-readable error when @p ok is false.
         *
         * This unblocks the network thread waiting on the request.
         */
        void completeCommand(uint64_t id, bool ok, const nlohmann::json &result,
                             const std::string &error);

        /** @brief Returns a copy of the recent activity log. */
        std::vector<std::string> getLog() const;

        /** @brief Clears the activity log. */
        void clearLog();

        /** @brief Number of MCP requests handled since construction. */
        unsigned long long getRequestCount() const { return requestCount.load(); }

        /** @brief Returns the static MCP tools/list payload (names, descriptions, schemas). */
        static nlohmann::json buildToolList();

    private:
        /** @brief Worker thread entry point: bind, listen and serve until stop(). */
        void run();

        /**
         * @brief Handles one JSON-RPC message.
         * @param request    Parsed request object.
         * @param hasResponse Set to false for notifications (no HTTP body is sent).
         * @param sessionId  Receives a session id for initialize requests.
         * @return JSON-RPC response object (valid only when hasResponse is true).
         */
        nlohmann::json processRpc(const nlohmann::json &request, bool &hasResponse,
                                  std::string &sessionId);

        /** @brief Dispatches a tools/call by name, blocking for main-thread tools. */
        nlohmann::json invokeTool(const std::string &name, const nlohmann::json &args);

        /**
         * @brief Queues a tool for execution on the editor's main thread and waits for it.
         * @return true when the main thread completed the command in time.
         */
        bool executeQueued(const std::string &name, const nlohmann::json &args,
                           nlohmann::json &result, std::string &error);

        /** @brief Updates the lifecycle state under the state mutex. */
        void setState(McpServerState newState, const std::string &error = "");

        /** @brief Appends a timestamped line to the activity log (thread-safe). */
        void log(const std::string &line);

        McpServerConfig config;                      ///< Copy of the settings used by the current run.
        std::thread worker;                          ///< Listener thread.
        std::atomic<bool> running{false};            ///< True while the socket is listening.
        std::atomic<bool> stopRequested{false};      ///< Set by stop() to leave the accept loop.
        mutable std::mutex stateMutex;               ///< Guards state and lastError.
        McpServerState state = McpServerState::Stopped; ///< Lifecycle state.
        std::string lastError;                       ///< Last startup error text.

        mutable std::mutex snapshotMutex;            ///< Guards statusSnapshot.
        nlohmann::json statusSnapshot;               ///< Last editor state snapshot.

        mutable std::mutex logMutex;                 ///< Guards logLines.
        std::deque<std::string> logLines;            ///< Recent activity lines (bounded).

        mutable std::mutex queueMutex;               ///< Guards pendingCommands/completedCommands.
        std::condition_variable queueCv;             ///< Signalled when a command completes.
        std::deque<McpCommand> pendingCommands;      ///< Commands waiting for the main thread.
        struct CommandResult { bool ok; nlohmann::json result; std::string error; };
        std::map<uint64_t, CommandResult> completedCommands; ///< Results waiting for the network thread.
        std::atomic<uint64_t> nextCommandId{1};      ///< Command id generator.

        std::atomic<unsigned long long> requestCount{0}; ///< Handled JSON-RPC request count.
};

#endif
