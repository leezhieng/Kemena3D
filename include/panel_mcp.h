#ifndef PANEL_MCP_H
#define PANEL_MCP_H

#include "kemena/kemena.h"

#include "manager.h"
#include "mcp_server.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>

/**
 * @brief Editor panel that configures and controls the embedded MCP server.
 *
 * The panel owns the McpServer instance. Each frame update() drains tool
 * commands queued by MCP clients and executes them on the main thread, keeping
 * all editor/engine access single-threaded. draw() renders the settings form,
 * start/stop controls, the advertised tool list and an activity log.
 */
class PanelMcp
{
    public:
        /**
         * @brief Constructs the panel and loads persisted settings.
         * @param setGuiManager GUI manager used to issue ImGui draw calls.
         * @param setManager Editor manager providing project/scene state.
         */
        PanelMcp(kGuiManager *setGuiManager, Manager *setManager);

        /** @brief Stops the MCP server if it is still running. */
        ~PanelMcp();

        /**
         * @brief Per-frame main-thread pump.
         *
         * Publishes an editor status snapshot for read-only tools and executes
         * pending tool commands. Must be called every frame, even when the
         * panel window is hidden, so MCP clients keep working.
         */
        void update();

        /**
         * @brief Renders the MCP Server window.
         * @param opened Reference to the panel visibility flag.
         */
        void draw(bool &opened);

        bool focused = false; ///< True while this panel had focus in the last frame.

    private:
        /** @brief Returns the config file path for the current project context. */
        std::string configPath() const;

        /** @brief Loads settings from configPath() when the file exists. */
        void loadConfig();

        /** @brief Saves the current settings to configPath(). */
        void saveConfig();

        /** @brief Builds the McpServerConfig from the UI fields. */
        McpServerConfig currentConfig() const;

        /** @brief Builds the JSON status snapshot exposed to MCP tools. */
        nlohmann::json buildSnapshot() const;

        /** @brief Executes one queued tool command on the main thread. */
        bool executeCommand(const McpCommand &command, nlohmann::json &result,
                            std::string &error);

        /** @brief Shows/hides a named editor panel. */
        bool applyPanelVisibility(const std::string &panel, bool visible,
                                  std::string &error);

        Manager *manager = nullptr; ///< Editor manager.
        kGuiManager *gui = nullptr; ///< GUI manager.
        McpServer server;           ///< Embedded MCP server instance.

        // Persisted settings (mirrored into McpServerConfig on start).
        std::string host = "127.0.0.1";
        int port = 8765;
        std::string endpoint = "/mcp";
        std::string serverName = "Kemena3D Editor";
        std::string serverVersion = "1.0.0";
        bool authEnabled = false;
        std::string authToken;

        std::string configMessage;             ///< Last load/save result shown in the panel.
        bool configMessageIsError = false;     ///< Colours configMessage red when true.

        bool lastProjectOpened = false;        ///< Tracks project open/close to reload settings.
        bool projectStateKnown = false;        ///< False until the first update() ran.
        McpServerState lastServerState = McpServerState::Stopped; ///< Detects state changes for console logging.

        bool autoScrollLog = true;             ///< Follow the newest activity line.
        std::string copiedUntil;               ///< "Copied!" feedback text with an expiry time.
        std::chrono::steady_clock::time_point copiedExpiry;
};

#endif
