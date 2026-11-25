#include "panel_mcp.h"

#include "mainmenu.h" // showPanel / pendingFocusWindow globals

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
    std::string toLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string normalizeEndpoint(std::string endpoint)
    {
        if (endpoint.empty())
            endpoint = "/mcp";
        if (endpoint.front() != '/')
            endpoint.insert(endpoint.begin(), '/');
        while (endpoint.size() > 1 && endpoint.back() == '/')
            endpoint.pop_back();
        return endpoint;
    }

    std::string previewUrl(const std::string &host, int port, const std::string &endpoint)
    {
        std::string displayHost = host.empty() ? "127.0.0.1" : host;
        if (displayHost == "0.0.0.0")
            displayHost = "127.0.0.1";
        return "http://" + displayHost + ":" + std::to_string(port) +
               normalizeEndpoint(endpoint);
    }

    kVec4 statusColor(McpServerState state)
    {
        switch (state)
        {
        case McpServerState::Running:  return kVec4(0.42f, 0.85f, 0.45f, 1.0f);
        case McpServerState::Starting: return kVec4(0.95f, 0.75f, 0.30f, 1.0f);
        case McpServerState::Error:    return kVec4(1.00f, 0.35f, 0.35f, 1.0f);
        case McpServerState::Stopped:
        default:                       return kVec4(0.65f, 0.66f, 0.70f, 1.0f);
        }
    }
}

PanelMcp::PanelMcp(kGuiManager *setGuiManager, Manager *setManager)
    : manager(setManager), gui(setGuiManager)
{
    loadConfig();
}

PanelMcp::~PanelMcp()
{
    server.stop();
}

std::string PanelMcp::configPath() const
{
    if (manager && manager->projectOpened && !manager->projectPath.empty())
        return (manager->projectPath / "Config" / "mcp.json").string();
    return "mcp_config.json";
}

void PanelMcp::loadConfig()
{
    const std::string path = configPath();
    std::ifstream stream(path);
    if (!stream.is_open())
        return; // No saved settings yet; keep defaults.

    const std::string content((std::istreambuf_iterator<char>(stream)),
                              std::istreambuf_iterator<char>());
    nlohmann::json settings = nlohmann::json::parse(content, nullptr, false);
    if (settings.is_discarded() || !settings.is_object())
    {
        configMessage = "Could not parse " + path;
        configMessageIsError = true;
        return;
    }

    host = settings.value("host", host);
    port = settings.value("port", port);
    endpoint = settings.value("endpoint", endpoint);
    serverName = settings.value("serverName", serverName);
    serverVersion = settings.value("serverVersion", serverVersion);
    authEnabled = settings.value("authEnabled", authEnabled);
    authToken = settings.value("authToken", authToken);

    if (port < 1 || port > 65535)
        port = 8765;
    endpoint = normalizeEndpoint(endpoint);

    configMessage = "Loaded settings from " + path;
    configMessageIsError = false;
}

void PanelMcp::saveConfig()
{
    const std::string path = configPath();
    const std::filesystem::path filePath(path);
    std::error_code ec;
    if (filePath.has_parent_path())
        std::filesystem::create_directories(filePath.parent_path(), ec);
    if (ec)
    {
        configMessage = "Could not create " + filePath.parent_path().string();
        configMessageIsError = true;
        return;
    }

    std::ofstream stream(path, std::ios::trunc);
    if (!stream.is_open())
    {
        configMessage = "Could not write " + path;
        configMessageIsError = true;
        return;
    }

    nlohmann::json settings = {
        {"host", host},
        {"port", port},
        {"endpoint", normalizeEndpoint(endpoint)},
        {"serverName", serverName},
        {"serverVersion", serverVersion},
        {"authEnabled", authEnabled},
        {"authToken", authToken}};

    stream << settings.dump(2) << std::endl;
    configMessage = "Saved settings to " + path;
    configMessageIsError = false;
}

McpServerConfig PanelMcp::currentConfig() const
{
    McpServerConfig config;
    config.host = host;
    config.port = port;
    config.endpoint = normalizeEndpoint(endpoint);
    config.serverName = serverName;
    config.serverVersion = serverVersion;
    config.authEnabled = authEnabled && !authToken.empty();
    config.authToken = authToken;
    return config;
}

nlohmann::json PanelMcp::buildSnapshot() const
{
    nlohmann::json snapshot;
    snapshot["server"] = {
        {"name", serverName},
        {"version", serverVersion},
        {"state", server.getStateString()},
        {"running", server.isRunning()},
        {"url", previewUrl(host, port, endpoint)},
        {"port", port},
        {"endpoint", normalizeEndpoint(endpoint)},
        {"authEnabled", authEnabled && !authToken.empty()},
        {"requestCount", server.getRequestCount()}};

    snapshot["panels"] = {
        {"world", showPanel.world},
        {"inspector", showPanel.inspector},
        {"hierarchy", showPanel.hierarchy},
        {"project", showPanel.project},
        {"console", showPanel.console},
        {"shaderEditor", showPanel.shaderEditor},
        {"scriptEditor", showPanel.scriptEditor},
        {"game", showPanel.game},
        {"animatorEditor", showPanel.animatorEditor},
        {"animationEditor", showPanel.animationEditor},
        {"particleEditor", showPanel.particleEditor},
        {"guiEditor", showPanel.guiEditor},
        {"mcp", showPanel.mcp}};

    nlohmann::json editor = {
        {"projectOpened", manager ? manager->projectOpened : false},
        {"projectName", manager ? manager->projectName : std::string()},
        {"projectPath", (manager && !manager->projectPath.empty())
                            ? manager->projectPath.string()
                            : std::string()},
        {"sceneObjectCount", 0},
        {"selectedObjects", nlohmann::json::array()}};

    if (manager)
    {
        if (kScene *scene = manager->getScene())
            editor["sceneObjectCount"] = scene->getObjects().size();
        for (const auto &uuid : manager->selectedObjects)
            editor["selectedObjects"].push_back(uuid);
    }

    snapshot["editor"] = editor;
    return snapshot;
}

void PanelMcp::update()
{
    // Keep the read-only tool snapshot in sync with the editor. The snapshot is
    // only consumed by network requests, so skip the per-frame allocation while
    // the server is stopped.
    if (server.isRunning())
        server.setStatusSnapshot(buildSnapshot());

    // Drain tool calls that need to run on the main thread.
    McpCommand command;
    while (server.pollCommand(command))
    {
        nlohmann::json result = nlohmann::json::object();
        std::string error;
        const bool ok = executeCommand(command, result, error);
        server.completeCommand(command.id, ok, result, error);
    }

    // Log lifecycle changes to the editor console so users notice clients
    // connecting or a failed start without watching the panel.
    const McpServerState state = server.getState();
    if (state != lastServerState)
    {
        if (state == McpServerState::Running && manager && manager->panelConsole)
            manager->panelConsole->addLog(LogLevel::Info, "[MCP] Server listening on %s",
                                          previewUrl(host, port, endpoint).c_str());
        else if (state == McpServerState::Error && manager && manager->panelConsole)
            manager->panelConsole->addLog(LogLevel::Error, "[MCP] %s",
                                          server.getLastError().c_str());
        else if (state == McpServerState::Stopped && manager && manager->panelConsole)
            manager->panelConsole->addLog(LogLevel::Info, "[MCP] Server stopped");
        lastServerState = state;
    }

    // When a project is opened or closed the settings file location changes;
    // reload it if the project provides its own Config/mcp.json.
    const bool projectOpened = manager && manager->projectOpened;
    if (!projectStateKnown || projectOpened != lastProjectOpened)
    {
        projectStateKnown = true;
        lastProjectOpened = projectOpened;
        std::ifstream probe(configPath());
        if (probe.is_open())
        {
            probe.close();
            if (server.isRunning())
                server.stop();
            loadConfig();
        }
    }
}

bool PanelMcp::applyPanelVisibility(const std::string &panel, bool visible,
                                    std::string &error)
{
    const std::string key = toLowerCopy(panel);
    const char *focusWindow = nullptr;

    if (key == "world")
    {
        showPanel.world = visible;
        focusWindow = "World";
    }
    else if (key == "inspector")
    {
        showPanel.inspector = visible;
        focusWindow = "Inspector";
    }
    else if (key == "hierarchy")
    {
        showPanel.hierarchy = visible;
        focusWindow = "Hierarchy";
    }
    else if (key == "project")
    {
        showPanel.project = visible;
        focusWindow = "Project";
    }
    else if (key == "console")
    {
        showPanel.console = visible;
        focusWindow = "Console";
    }
    else if (key == "shadereditor")
    {
        showPanel.shaderEditor = visible;
        focusWindow = "Shader";
    }
    else if (key == "scripteditor")
    {
        showPanel.scriptEditor = visible;
        focusWindow = "Logic Graph";
    }
    else if (key == "game")
    {
        showPanel.game = visible;
        focusWindow = "Game";
    }
    else if (key == "animatoreditor")
    {
        showPanel.animatorEditor = visible;
        focusWindow = "AnimatorEditor";
    }
    else if (key == "animationeditor")
    {
        showPanel.animationEditor = visible;
        focusWindow = "CinematicEditor";
    }
    else if (key == "particleeditor")
    {
        showPanel.particleEditor = visible;
        focusWindow = "Particle Editor";
    }
    else if (key == "guieditor")
    {
        showPanel.guiEditor = visible;
        focusWindow = "IngameUI";
    }
    else if (key == "mcp")
    {
        showPanel.mcp = visible;
        focusWindow = "MCP Server";
    }
    else
    {
        error = "Unknown panel '" + panel +
                "'. Expected one of: world, inspector, hierarchy, project, "
                "console, shaderEditor, scriptEditor, game, animatorEditor, "
                "animationEditor, particleEditor, guiEditor, mcp.";
        return false;
    }

    if (visible && focusWindow)
        pendingFocusWindow = focusWindow;
    return true;
}

bool PanelMcp::executeCommand(const McpCommand &command, nlohmann::json &result,
                              std::string &error)
{
    if (command.tool == "kemena_set_panel_visibility")
    {
        const std::string panel = command.arguments.value("panel", "");
        const bool visible = command.arguments.value("visible", true);
        if (panel.empty())
        {
            error = "Missing required argument 'panel'.";
            return false;
        }
        if (!applyPanelVisibility(panel, visible, error))
            return false;
        result = {{"panel", panel}, {"visible", visible}};
        return true;
    }

    if (command.tool == "kemena_console_log")
    {
        const std::string message = command.arguments.value("message", "");
        if (message.empty())
        {
            error = "Missing required argument 'message'.";
            return false;
        }
        if (!manager || !manager->panelConsole)
        {
            error = "The Console panel is not available.";
            return false;
        }

        const std::string level = toLowerCopy(command.arguments.value("level", "info"));
        LogLevel logLevel = LogLevel::Info;
        if (level == "warning")
            logLevel = LogLevel::Warning;
        else if (level == "error")
            logLevel = LogLevel::Error;

        manager->panelConsole->addLog(logLevel, "[MCP] %s", message.c_str());
        result = {{"logged", true}, {"level", level}};
        return true;
    }

    if (command.tool == "kemena_list_scene_objects")
    {
        nlohmann::json objects = nlohmann::json::array();
        if (manager)
        {
            if (kScene *scene = manager->getScene())
            {
                for (kObject *object : scene->getObjects())
                {
                    if (!object)
                        continue;
                    objects.push_back({
                        {"uuid", object->getUuid()},
                        {"name", object->getName()},
                        {"type", static_cast<int>(object->getType())}});
                }
            }
        }

        result = {{"scene", manager ? "active" : "unavailable"},
                  {"count", objects.size()},
                  {"objects", objects}};
        return true;
    }

    if (command.tool == "kemena_select_object")
    {
        const std::string uuid = command.arguments.value("uuid", "");
        if (uuid.empty())
        {
            error = "Missing required argument 'uuid'.";
            return false;
        }
        if (!manager)
        {
            error = "Editor is not available.";
            return false;
        }

        const bool clearSelection = command.arguments.value("clearSelection", true);
        manager->selectObject(uuid, clearSelection);
        result = {{"uuid", uuid}, {"clearSelection", clearSelection}};
        return true;
    }

    error = "Unsupported tool: " + command.tool;
    return false;
}

void PanelMcp::draw(bool &opened)
{
    focused = false;
    if (!opened)
        return;

    gui->windowStart("MCP Server", &opened);
    focused = gui->isWindowFocused();

    const McpServerState state = server.getState();
    const bool running = server.isRunning();
    const bool busy = state == McpServerState::Starting;
    const std::string displayUrl = previewUrl(host, port, endpoint);

    // ---- Status ----
    gui->textColored(statusColor(state), "Status: " + server.getStateString());
    gui->sameLine();
    gui->textDisabled("Requests: " + std::to_string(server.getRequestCount()));

    if (state == McpServerState::Error && !server.getLastError().empty())
        gui->textColored(statusColor(McpServerState::Error), server.getLastError());

    gui->textWrapped(
        "Exposes the editor to MCP clients over Streamable HTTP. Point an MCP "
        "client at the URL below; tool calls run on the editor's main thread.");

    gui->separatorText("Connection");

    gui->beginDisabled(running || busy);
    gui->inputText("Host", host, 64);
    gui->inputInt("Port", &port, 1, 100);
    if (port < 1)
        port = 1;
    else if (port > 65535)
        port = 65535;
    gui->inputText("Endpoint", endpoint, 64);
    gui->inputText("Server Name", serverName, 128);
    gui->inputText("Server Version", serverVersion, 32);
    gui->endDisabled();

    gui->labelText("URL", displayUrl);
    gui->sameLine();
    if (gui->smallButton("Copy URL"))
    {
        gui->setClipboardText(displayUrl);
        copiedExpiry = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
    if (std::chrono::steady_clock::now() < copiedExpiry)
    {
        gui->sameLine();
        gui->textDisabled("Copied!");
    }

    gui->separatorText("Authentication");
    gui->beginDisabled(running || busy);
    gui->checkbox("Require Bearer Token", &authEnabled);
    if (authEnabled)
    {
        gui->inputTextWithHint("Token", "shared secret", authToken, 256,
                               ImGuiInputTextFlags_Password);
        if (authToken.empty())
            gui->textColored(statusColor(McpServerState::Starting),
                             "Set a token or authentication stays disabled.");
    }
    gui->endDisabled();

    gui->separatorText("Control");
    gui->beginDisabled(busy);
    if (running)
    {
        if (gui->button("Stop Server", kIvec2(140, 0)))
            server.stop();
    }
    else if (gui->button("Start Server", kIvec2(140, 0)))
    {
        saveConfig();
        if (!server.start(currentConfig()) && manager && manager->panelConsole)
            manager->panelConsole->addLog(LogLevel::Error, "[MCP] %s",
                                          server.getLastError().c_str());
        // Publish a snapshot immediately so a client that connects before the
        // next frame still gets meaningful status data.
        server.setStatusSnapshot(buildSnapshot());
    }
    gui->endDisabled();

    gui->sameLine();
    gui->beginDisabled(running || busy);
    if (gui->button("Save Settings"))
        saveConfig();
    gui->sameLine();
    if (gui->button("Reload"))
        loadConfig();
    gui->endDisabled();

    if (!configMessage.empty())
    {
        gui->textColored(configMessageIsError ? statusColor(McpServerState::Error)
                                              : statusColor(McpServerState::Running),
                         configMessage);
    }

    // ---- Tool list ----
    const nlohmann::json tools = McpServer::buildToolList();
    gui->separatorText(("Tools (" + std::to_string(tools.size()) + ")").c_str());
    if (gui->collapsingHeader("Available MCP tools", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const auto &tool : tools)
        {
            const std::string name = tool.value("name", "");
            const std::string title = tool.value("title", "");
            if (gui->treeStart(name + (title.empty() ? "" : "  -  " + title)))
            {
                gui->textWrapped(tool.value("description", ""));
                gui->treeEnd();
            }
        }
    }

    // ---- Activity log ----
    gui->separatorText("Activity");
    gui->checkbox("Auto-scroll", &autoScrollLog);
    gui->sameLine();
    if (gui->smallButton("Clear"))
        server.clearLog();

    const std::vector<std::string> lines = server.getLog();
    gui->childStart("##McpActivityLog", kVec2(0, 180), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
    if (lines.empty())
        gui->textDisabled("No activity yet.");
    else
        for (const auto &line : lines)
            gui->textWrapped(line);
    if (autoScrollLog)
        gui->setScrollHereY(1.0f);
    gui->childEnd();

    gui->windowEnd();
}
