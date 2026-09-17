#include "manager.h"
#include "util.h"
#include "panel_logicgraph.h" // for panelLogicGraph->notifyAssetMoved()
#include "panel_shadergraph.h" // for panelShaderGraph->getFilePath()
#include "mainmenu.h" // for showPanel / savedWorkspaceFileName

#include <kemena/kpackage.h>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include <stb/stb_image_resize2.h>

#include <algorithm> // std::find, std::remove

#include <kemena/kmesh.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>
#include <kemena/kdecal.h>
#include <kemena/kmeshgenerator.h>
#include <kemena/kshadernode.h>
#include <kemena/kscriptgraph.h>
#include <kemena/kinputmanager.h>

#include <miniaudio.h>
// Custom decoding backends provided by the Kemena3D SDK
#include "miniaudio_libvorbis.h"
#include "miniaudio_libopus.h"

#include <set>
#include <functional>
#include <ctime>
#include <cstdlib>
#include <cmath>

namespace fs = std::filesystem;

Manager::Manager(kWindow *setWindow, kWorld *setWorld, kRenderer *setRenderer)
{
    window = setWindow;
    world = setWorld;
    renderer = setRenderer;
    initialWindowTitle = window->getWindowTitle();

    try
    {
#ifdef _WIN32
        char buffer[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, buffer, MAX_PATH);
        exePath = kString(buffer, len);
#elif __APPLE__
        char buffer[PATH_MAX];
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) == 0)
            exePath = fs::canonical(buffer).string();
        else
            exePath.clear();
#elif __linux__
        char buffer[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1)
        {
            buffer[len] = '\0';
            exePath = fs::canonical(buffer).string();
        }
        else
        {
            exePath.clear();
        }
#else
        exePath = fs::current_path().string(); // fallback for unknown platforms
#endif

        if (!exePath.empty())
            baseDir = fs::path(exePath).parent_path().string();
        else
            baseDir = fs::current_path().string(); // fallback
    }
    catch (const fs::filesystem_error &e)
    {
        std::cerr << "Error resolving executable path: " << e.what() << std::endl;
        exePath.clear();
        baseDir = fs::current_path().string(); // fallback
    }

    loadRecentProjects();

    // The editor drives the named input system during Play mode so AngelScript
    // and logic-graph scripts can query getAction()/getAxis().
    inputManager = new kInputManager();
    inputManager->init();

    // Default tag / physics-layer lists (refreshed from config on project open).
    loadTagSettings();
    loadLayerSettings();
}

Manager::~Manager()
{
    if (inputManager)
    {
        inputManager->shutdown();
        delete inputManager;
        inputManager = nullptr;
    }

    if (gameAudioManager)
    {
        // Stop any clips still playing and unload them, then tear down the
        // engine.  No new Play/Stop cycles can start after the destructor
        // runs, so we don't need the re-entrancy guard used in stopGameAudio.
        for (auto &entry : gameAudioEntries)
        {
            if (entry.clip)
            {
                entry.clip->stop();
                gameAudioManager->unloadAudio(entry.clip);
            }
        }
        gameAudioEntries.clear();
        gameAudioManager->shutdown();
        delete gameAudioManager;
        gameAudioManager = nullptr;
    }
}

kString Manager::getCurrentDirPath()
{
    fs::path path = projectPath;

    for (const auto &dir : currentDir)
    {
        path /= dir;
    }

    return path.string();
}

void Manager::openFolder(kString name)
{
    currentDir.push_back(name);
}

void Manager::closeFolder()
{
    if (currentDir.size() > 1)
    {
        currentDir.pop_back();
    }
}

bool Manager::newProject()
{
    // Check if project is saved
    if (!projectSaved)
    {
        showingMessageBox = true;

        auto result = pfd::message(
                          "Unsaved Changes",
                          "You have unsaved changes. Do you want to create a new project without saving?",
                          pfd::choice::yes_no,
                          pfd::icon::warning)
                          .result();

        if (result == pfd::button::no)
            return false;
    }

    // Open the New Project dialog instead of the native folder selector.
    // The dialog captures project name, directory, and create-folder preference.
    newProjectNameInput = "New Project";
    newProjectDirInput.clear();
    newProjectCreateFolder = true;
    showNewProjectDialog = true;
    return true; // dialog is now visible; actual creation happens in executeNewProject()
}

void Manager::drawNewProjectDialog()
{
    if (!showNewProjectDialog)
        return;

    ImGui::OpenPopup("New Project");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 220), ImGuiCond_FirstUseEver);

    if (!ImGui::BeginPopupModal("New Project", &showNewProjectDialog,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
        return;

    // --- Project Name ---
    char nameBuf[256];
    strncpy_s(nameBuf, sizeof(nameBuf), newProjectNameInput.c_str(), _TRUNCATE);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::TextUnformatted("Project Name:");
    ImGui::SameLine(120.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##projName", nameBuf, sizeof(nameBuf)))
        newProjectNameInput = nameBuf;

    ImGui::Spacing();

    // --- Directory ---
    char dirBuf[1024];
    strncpy_s(dirBuf, sizeof(dirBuf), newProjectDirInput.string().c_str(), _TRUNCATE);
    dirBuf[sizeof(dirBuf) - 1] = '\0';
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 120.0f);
    ImGui::TextUnformatted("Directory:");
    ImGui::SameLine(120.0f);
    float dirInputW = ImGui::GetContentRegionAvail().x - 40.0f;
    ImGui::SetNextItemWidth(dirInputW);
    if (ImGui::InputText("##projDir", dirBuf, sizeof(dirBuf)))
        newProjectDirInput = dirBuf;

    ImGui::SameLine();
    if (ImGui::SmallButton("...##browseDir"))
    {
        auto sel = pfd::select_folder("Choose project parent directory").result();
        if (!sel.empty())
            newProjectDirInput = sel;
    }

    ImGui::Spacing();

    // --- Create Project Folder checkbox ---
    ImGui::Checkbox("Create Project Folder", &newProjectCreateFolder);
    ImGui::TextDisabled("  When checked, a subfolder matching the project name will be created.");
    ImGui::TextDisabled("  When unchecked, the selected directory is used directly.");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Preview path ---
    if (!newProjectNameInput.empty() && !newProjectDirInput.empty())
    {
        fs::path previewPath = newProjectDirInput;
        if (newProjectCreateFolder)
            previewPath /= newProjectNameInput;
        ImGui::TextDisabled("  Project will be created at: %s", previewPath.string().c_str());
    }

    ImGui::Spacing();

    // --- Buttons ---
    bool canCreate = !newProjectNameInput.empty() && !newProjectDirInput.empty()
                     && fs::exists(newProjectDirInput) && fs::is_directory(newProjectDirInput);

    if (!canCreate)
        ImGui::BeginDisabled();

    if (ImGui::Button("Create", ImVec2(120, 0)))
    {
        if (executeNewProject(newProjectNameInput, newProjectDirInput, newProjectCreateFolder))
        {
            ImGui::CloseCurrentPopup();
            showNewProjectDialog = false;
        }
    }

    if (!canCreate)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
        ImGui::CloseCurrentPopup();
        showNewProjectDialog = false;
    }

    ImGui::EndPopup();
}

bool Manager::executeNewProject(const kString& name, const fs::path& dir, bool createFolder)
{
    if (name.empty() || dir.empty())
        return false;

    if (!fs::exists(dir) || !fs::is_directory(dir))
    {
        kString msg = "Directory does not exist:\n" + dir.string();
        pfd::message("Invalid Directory", msg, pfd::choice::ok, pfd::icon::warning).result();
        return false;
    }

    fs::path fullPath;
    if (createFolder)
    {
        fullPath = dir / name;

        // Check if the project folder already exists
        if (fs::exists(fullPath))
        {
            kString msg = "A folder named \"" + name + "\" already exists in:\n"
                        + dir.string() + "\n\nPlease choose a different project name or directory.";
            pfd::message("Folder Exists", msg, pfd::choice::ok, pfd::icon::warning).result();
            return false;
        }
    }
    else
    {
        fullPath = dir;
    }

    std::error_code ec;

    // Create required subfolders
    fs::create_directories(fullPath / "Assets", ec);
    fs::create_directories(fullPath / "Library", ec);
    fs::create_directories(fullPath / "Library" / "Metadata", ec);
    fs::create_directories(fullPath / "Library" / "Thumbnails", ec);
    fs::create_directories(fullPath / "Library" / "ImportedAssets", ec);
    fs::create_directories(fullPath / "Config", ec);

    // Save project name into Config/project.json
    projectName = name;
    projectOpened = true;
    projectSaved = false;
    refreshWindowTitle();

    if (!renderer->getEnableObjectPicking())
        renderer->setEnableObjectPicking(true);

    projectPath = fullPath;
    currentDir.clear();
    currentDir.push_back("Assets");

    // Load this project's workspace layout (falls back to the default).
    loadProjectWorkspace();

    // Save the project name to config immediately
    saveProjectConfig();

    // Initialize publish settings with the project name
    publishSettings = PublishSettings();
    for (int i = 0; i < 3; ++i)
    {
        publishSettings.platforms[i].gameName = name;
        publishSettings.platforms[i].title = name;
    }
    savePublishSettings();

    // Initialize tag / physics-layer settings with sensible defaults.
    loadTagSettings();
    loadLayerSettings();
    saveTagSettings();
    saveLayerSettings();

    // Clear any previously opened world and start fresh.
    resetToFreshWorld();

    checkAssetChange();

    if (panelProject != nullptr)
    {
        panelProject->clearSelection();
        panelProject->refreshTreeList();
        panelProject->refreshThumbnailList();
    }

    addRecentProject(projectPath.string());

    kString msg = "Project created at: " + fullPath.string();
    pfd::message("Success", msg, pfd::choice::ok, pfd::icon::info).result();

    return true;
}

bool Manager::openProject()
{
    showingMessageBox = true;

    // Check if project is saved
    if (!projectSaved)
    {
        auto result = pfd::message(
                          "Unsaved Changes",
                          "You have unsaved changes. Do you want to open a new project without saving?",
                          pfd::choice::yes_no,
                          pfd::icon::warning)
                          .result();

        if (result == pfd::button::no)
            return false;
    }

    auto path = pfd::select_folder("Select project folder").result();

    if (path.empty())
    {
        return false;
    }

    if (!fs::exists(path) || !fs::is_directory(path))
    {
        kString msg = "Directory does not exist:\n" + path;

        pfd::message(
            "Invalid Directory", // title
            msg,                 // message
            pfd::choice::ok,     // only an OK button
            pfd::icon::warning   // warning icon
            )
            .result();

        return false;
    }

    fs::path fullPath = fs::path(path);

    fs::path assetsPath = fullPath / "Assets";
    fs::path libraryPath = fullPath / "Library";
    fs::path configPath = fullPath / "Config";

    if (!(fs::exists(assetsPath) && fs::exists(libraryPath) && fs::exists(configPath)))
    {
        kString msg = "Failed to open project. Invalid directory structure.\n";

        pfd::message(
            "Invalid Directory", // title
            msg,                 // message
            pfd::choice::ok,     // only an OK button
            pfd::icon::warning   // warning icon
            )
            .result();

        return false;
    }

    // Open project successful

    // Extract the folder name, but prefer the saved project name if available.
    projectName = fullPath.filename().string();
    {
        fs::path cfgFile = fullPath / "Config" / "project.json";
        if (fs::exists(cfgFile))
        {
            try
            {
                std::ifstream cf(cfgFile);
                json cfg;
                cf >> cfg;
                if (cfg.contains("project_name") && cfg["project_name"].is_string())
                {
                    kString savedName = cfg["project_name"].get<kString>();
                    if (!savedName.empty())
                        projectName = savedName;
                }
            }
            catch (...) {}
        }
    }
    projectOpened = true;
    projectSaved = false;
    refreshWindowTitle();

    if (!renderer->getEnableObjectPicking())
        renderer->setEnableObjectPicking(true);

    projectPath = path;
    currentDir.clear();
    currentDir.push_back("Assets");

    // Load this project's workspace layout (falls back to the default).
    loadProjectWorkspace();

    // Create other essential folders if don't exist
    std::error_code ec;

    fs::create_directories(fullPath / "Assets", ec);
    fs::path metadataPath = fullPath / "Library" / "Metadata";
    if (!(fs::exists(metadataPath)))
        fs::create_directories(fullPath / "Library" / "Metadata", ec);

    fs::path thumbnailsPath = fullPath / "Library" / "Thumbnails";
    if (!(fs::exists(thumbnailsPath)))
        fs::create_directories(fullPath / "Library" / "Thumbnails", ec);

    fs::path importedAssetsPath = fullPath / "Library" / "ImportedAssets";
    if (!(fs::exists(importedAssetsPath)))
        fs::create_directories(fullPath / "Library" / "ImportedAssets", ec);

    // TODO: check project config file
    // Need this to false to check assets
    showingMessageBox = false;
    checkAssetChange();

    if (panelProject != nullptr)
    {
        panelProject->clearSelection();
        panelProject->refreshTreeList();
        panelProject->refreshThumbnailList();
    }

    // If a previous session recorded a last-opened world, load it now.
    // Otherwise start with a fresh default world.
    {
        fs::path last = loadLastWorldPath();
        if (!last.empty())
            loadWorld(last.string());
        else
            resetToFreshWorld();
    }

    addRecentProject(projectPath.string());

    return true;
}

bool Manager::openProjectFromPath(const kString &path)
{
    if (path.empty())
        return false;

    fs::path fullPath = fs::path(path);

    if (!fs::exists(fullPath) || !fs::is_directory(fullPath))
        return false;

    if (!(fs::exists(fullPath / "Assets") &&
          fs::exists(fullPath / "Library") &&
          fs::exists(fullPath / "Config")))
        return false;

    projectName = fullPath.filename().string();
    // Prefer the saved project name from config if available.
    {
        fs::path cfgFile = fullPath / "Config" / "project.json";
        if (fs::exists(cfgFile))
        {
            try
            {
                std::ifstream cf(cfgFile);
                json cfg;
                cf >> cfg;
                if (cfg.contains("project_name") && cfg["project_name"].is_string())
                {
                    kString savedName = cfg["project_name"].get<kString>();
                    if (!savedName.empty())
                        projectName = savedName;
                }
            }
            catch (...) {}
        }
    }
    projectOpened = true;
    projectSaved = false;
    refreshWindowTitle();

    if (!renderer->getEnableObjectPicking())
        renderer->setEnableObjectPicking(true);

    projectPath = path;
    currentDir.clear();
    currentDir.push_back("Assets");

    // Load this project's workspace layout (falls back to the default).
    loadProjectWorkspace();

    // Load project-level tag / physics-layer settings.
    loadTagSettings();
    loadLayerSettings();

    std::error_code ec;
    fs::create_directories(fullPath / "Assets", ec);

    if (!fs::exists(fullPath / "Library" / "Metadata"))
        fs::create_directories(fullPath / "Library" / "Metadata", ec);
    if (!fs::exists(fullPath / "Library" / "Thumbnails"))
        fs::create_directories(fullPath / "Library" / "Thumbnails", ec);
    if (!fs::exists(fullPath / "Library" / "ImportedAssets"))
        fs::create_directories(fullPath / "Library" / "ImportedAssets", ec);

    showingMessageBox = false;
    checkAssetChange();

    if (panelProject != nullptr)
    {
        panelProject->clearSelection();
        panelProject->refreshTreeList();
        panelProject->refreshThumbnailList();
    }

    // Auto-load the previously-opened world for this project (if any).
    // Otherwise start with a fresh default world.
    {
        fs::path last = loadLastWorldPath();
        if (!last.empty())
            loadWorld(last.string());
        else
            resetToFreshWorld();
    }

    addRecentProject(path);

    return true;
}

void Manager::loadRecentProjects()
{
    fs::path configFile = fs::path(baseDir) / "recent_projects.json";
    if (!fs::exists(configFile))
        return;

    try
    {
        std::ifstream f(configFile);
        json j = json::parse(f);
        recentProjects.clear();
        // Filter out empties, duplicates and paths that no longer exist —
        // older builds occasionally recorded a blank entry when project
        // creation was interrupted, and folders can be moved/deleted between
        // sessions.
        std::set<kString> seen;
        for (auto &entry : j["projects"])
        {
            kString p = entry.get<std::string>();
            if (p.empty() || !seen.insert(p).second)
                continue;
            if (!fs::exists(p))
                continue;
            recentProjects.push_back(std::move(p));
        }
    }
    catch (...)
    {
    }
}

void Manager::saveRecentProjects()
{
    fs::path configFile = fs::path(baseDir) / "recent_projects.json";

    try
    {
        json j;
        j["projects"] = recentProjects;
        std::ofstream f(configFile);
        f << j.dump(4);
    }
    catch (...)
    {
    }
}

void Manager::addRecentProject(const kString &path)
{
    if (path.empty())
        return;

    // Copy first: callers (the splash) frequently pass a reference straight
    // out of `recentProjects` — erase() below would otherwise invalidate the
    // referent, and the subsequent insert() would read garbage and store an
    // empty / corrupted entry. That's why the list went empty after opening
    // a recent project.
    kString p = path;

    auto it = std::find(recentProjects.begin(), recentProjects.end(), p);
    if (it != recentProjects.end())
        recentProjects.erase(it);

    recentProjects.insert(recentProjects.begin(), p);

    if (recentProjects.size() > 8)
        recentProjects.resize(8);

    saveRecentProjects();
}

void Manager::checkAssetChange()
{
    if (projectOpened && !showingMessageBox)
    {
        bool anyChanges = false;
        fileMap.clear();
        importTasks.clear();
        // Drop cached raw shaders / textures so edited assets are recompiled /
        // reloaded on next material build. (Old entries may still be referenced
        // by live materials, so we don't free them here — just stop reusing them.)
        shaderCache.clear();
        textureCache.clear();
        fs::path libraryFolder = projectPath / "Library";
        fs::path assetsJsonFile = libraryFolder / "assets.json";
        fs::path assetsPath = projectPath / "Assets";

        // Check whether assets.json exist or not
        try
        {
            if (!fs::exists(libraryFolder))
            {
                fs::create_directories(libraryFolder);
                std::cout << "Created Library folder: " << libraryFolder << "\n";
            }

            if (!fs::exists(assetsJsonFile))
            {
                json j;
                j["files"] = json::array();

                std::ofstream ofs(assetsJsonFile);
                ofs << j.dump(4); // pretty print
                ofs.close();

                std::cout << "Created assets.json at: " << assetsJsonFile << "\n";
            }
            else
            {
                std::cout << "assets.json already exists.\n";
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error: " << e.what() << "\n";
            return;
        }

        // Read assets.json
        // Check whether the files exist or not
        json j;
        // Check if file is empty
        if (fs::is_empty(assetsJsonFile))
        {
            std::cout << "assets.json is empty, reinitializing...\n";
            j["files"] = json::array();

            std::ofstream ofs(assetsJsonFile);
            ofs << j.dump(4);
            ofs.close();
        }
        else
        {
            std::ifstream ifs(assetsJsonFile);
            try
            {
                ifs >> j;
            }
            catch (const std::exception &e)
            {
                std::cerr << "Failed to parse assets.json: " << e.what() << "\n";
                j["files"] = json::array(); // reset to valid state
            }
        }

        if (!j.contains("files") || !j["files"].is_array())
        {
            std::cerr << "Invalid assets.json format\n";
            return;
        }

        if (!j["files"].empty())
        {
            for (auto it = j["files"].begin(); it != j["files"].end();)
            {
                kString uuid = (*it)["uuid"].get<kString>();
                kString relativePath = (*it)["name"].get<kString>();
                kString checksum = (*it).value("checksum", "");
                kString type = (*it)["type"].get<kString>();

                fs::path filePath = assetsPath / relativePath;

                if (!fs::exists(filePath))
                {
                    std::cout << "Missing: " << relativePath << " (removed from list)\n";
                    it = j["files"].erase(it);
                    anyChanges = true;

                    // Delete metadata, thumbnail and imported asset?

                    // Delete metadata
                    fs::path metadataFile = libraryFolder / "Metadata" / (uuid + ".json");
                    if (fs::exists(metadataFile))
                    {
                        try
                        {
                            if (fs::remove(metadataFile))
                            {
                                std::cout << "Deleted file: " << metadataFile << "\n";
                            }
                            else
                            {
                                std::cout << "Failed to delete file (unknown reason): " << metadataFile << "\n";
                            }
                        }
                        catch (const fs::filesystem_error &e)
                        {
                            std::cerr << "Error deleting file: " << e.what() << "\n";
                        }
                    }
                    else
                    {
                        std::cout << "File does not exist: " << metadataFile << "\n";
                    }

                    // Delete imported assets and thumbnail
                    kString assetExt;
                    if (type == "mesh")
                        assetExt = ".glb";
                    else if (type == "image")
                        assetExt = ".dds";

                    auto tryDelete = [](const fs::path &p)
                    {
                        if (!fs::exists(p))
                            return;
                        try
                        {
                            fs::remove(p);
                            std::cout << "Deleted: " << p << "\n";
                        }
                        catch (const fs::filesystem_error &e)
                        {
                            std::cerr << "Error deleting: " << e.what() << "\n";
                        }
                    };

                    if (!assetExt.empty())
                        tryDelete(libraryFolder / "ImportedAssets" / (uuid + assetExt));
                    else if (type == "audio")
                    {
                        // Audio files keep their original extension; scan for any match.
                        for (const char* ext : {".wav", ".ogg", ".mp3", ".flac"})
                        {
                            fs::path cand = libraryFolder / "ImportedAssets" / (uuid + ext);
                            if (fs::exists(cand))
                                { tryDelete(cand); break; }
                        }
                    }

                    tryDelete(libraryFolder / "Thumbnails" / (uuid + ".png"));

                    continue;
                }

                // Fill struct and store in map
                FileInfo info{relativePath, checksum, type};
                fileMap[uuid] = info;

                ++it;
            }
        }

        // Build a reverse lookup from path -> uuid for convenience
        uuidMap.clear();
        for (const auto &[uuid, info] : fileMap)
            uuidMap[info.path] = uuid;

        // Check all files in the Assets folder
        for (auto &p : fs::recursive_directory_iterator(assetsPath))
        {
            if (!p.is_regular_file())
                continue;

            kString relativePath = fs::relative(p.path(), assetsPath).generic_string();
            kString checksum = generateFileChecksum(p.path().string());

            kString fileUuid;
            kString fileType;
            bool needImport = false;

            // Check with assets.json
            auto it = uuidMap.find(relativePath);
            if (it == uuidMap.end())
            {
                // New file
                kString uuid = generateUuid();
                kString type = checkAssetType(p.path());

                fileUuid = uuid;
                fileType = type;
                needImport = true; // Need import

                FileInfo info{relativePath, checksum, type};
                fileMap[uuid] = info;
                uuidMap[relativePath] = uuid;

                json newEntry =
                    {
                        {"name", relativePath},
                        {"uuid", uuid},
                        {"checksum", checksum},
                        {"type", type}};
                j["files"].push_back(newEntry);

                std::cout << "New file added: " << relativePath << "\n";
            }
            else
            {
                // Existing file, check checksum
                kString uuid = it->second;
                FileInfo &info = fileMap[uuid];

                fileUuid = uuid;
                fileType = info.type;

                // Different checksum
                if (info.checksum != checksum)
                {
                    std::cout << "File changed: " << relativePath << "\n";
                    info.checksum = checksum;
                    needImport = true; // Need import

                    // Update JSON as well
                    for (auto &entry : j["files"])
                    {
                        if (entry["uuid"] == uuid)
                        {
                            entry["checksum"] = checksum;
                            break;
                        }
                    }
                }
            }

            if (!fileUuid.empty() && !fileType.empty())
            {
                // Determine dest extension
                kString uuidExt;
                if (fileType == "mesh")
                    uuidExt = ".glb";
                else if (fileType == "image")
                    uuidExt = ".dds";

                fs::path srcFullPath = assetsPath / relativePath;

                if (fileType == "audio")
                    uuidExt = ".wav"; // always transcode to 16-bit PCM WAV
                fs::path destDir = libraryFolder / "ImportedAssets";
                fs::path destFile = destDir / (fileUuid + uuidExt);
                fs::path thumbnailPath = libraryFolder / "Thumbnails" / (fileUuid + ".png");
                fs::path metaPath = libraryFolder / "Metadata" / (fileUuid + ".json");

                // Write / overwrite metadata whenever it's missing or the file changed.
                // Preserve any existing user-chosen import settings (scaleFactor,
                // tangents, importAnimation, etc.) so a source change or a fresh
                // re-import on another machine doesn't silently reset them.
                if (!fs::exists(metaPath) || needImport)
                {
                    nlohmann::json meta;
                    if (fs::exists(metaPath))
                    {
                        try
                        {
                            std::ifstream existing(metaPath);
                            existing >> meta;
                            if (!meta.is_object())
                                meta = nlohmann::json::object();
                        }
                        catch (...)
                        {
                            meta = nlohmann::json::object();
                        }
                    }

                    meta["type"] = fileType;
                    meta["last_change"] = static_cast<int64_t>(std::time(nullptr));
                    meta["src_checksum"] = checksum;
                    meta["src_path"] = fs::relative(srcFullPath.parent_path(), projectPath).generic_string();
                    meta["src_file"] = srcFullPath.filename().generic_string();
                    meta["dest_path"] = fs::relative(destDir, projectPath).generic_string();
                    meta["dest_file"] = fileUuid + uuidExt;

                    std::ofstream mf(metaPath);
                    if (mf)
                    {
                        mf << meta.dump(4);
                        mf.close();
                    }
                    else
                        std::cerr << "Failed to write metadata: " << metaPath << "\n";
                }

                // Queue conversion if imported asset is missing or source changed.
                // Only mesh/image produce a file in ImportedAssets (uuidExt set).
                // Materials and other types have no imported file, so testing
                // destFile here would be testing "ImportedAssets/<uuid>" with no
                // extension — which never exists — and would spuriously force a
                // re-import (and thumbnail regen) on every focus/scan.
                if (!uuidExt.empty() && !fs::exists(destFile))
                    needImport = true;

                // If the thumbnail is missing, force a re-import/re-generation.
                if (!fs::exists(thumbnailPath))
                {
                    if (fileType == "mesh" && !uuidExt.empty())
                    {
                        // For meshes, delete the imported asset and metadata to trigger full re-import.
                        auto tryRemove = [](const fs::path &p)
                        {
                            if (!fs::exists(p))
                                return;
                            std::error_code ec;
                            fs::remove(p, ec);
                            if (!ec)
                                std::cout << "Removed stale Library file: " << p << "\n";
                            else
                                std::cerr << "Failed to remove: " << p << " (" << ec.message() << ")\n";
                        };
                        tryRemove(destFile);
                        tryRemove(metaPath);
                        needImport = true;
                    }
                    else if (fileType == "image" && fs::exists(srcFullPath) && !needImport)
                    {
                        // For images, just queue a thumbnail generation from the source file.
                        thumbnailQueue.push_back({fileUuid, srcFullPath, thumbnailPath, "image"});
                        anyChanges = true;
                    }
                }

                if (needImport && (fileType == "mesh" || fileType == "image" || fileType == "audio"))
                {
                    std::cout << srcFullPath << " -> " << destFile << "\n";
                    importTasks.push_back({srcFullPath, destFile, fileType,
                                           fileUuid, thumbnailPath, false, false});
                    anyChanges = true;
                }

                if (fileType == "material" && (!fs::exists(thumbnailPath) || needImport))
                {
                    if (fs::exists(thumbnailPath))
                        fs::remove(thumbnailPath);
                    thumbnailQueue.push_back({fileUuid, srcFullPath, thumbnailPath, "material"});
                    anyChanges = true;
                }

                if (fileType == "audio" && (!fs::exists(thumbnailPath) || needImport))
                {
                    if (fs::exists(thumbnailPath))
                        fs::remove(thumbnailPath);
                    thumbnailQueue.push_back({fileUuid, srcFullPath, thumbnailPath, "audio"});
                    anyChanges = true;
                }
            }
        }

        // Save back
        std::ofstream out(assetsJsonFile);
        out << j.dump(4);
        std::cout << "assets.json updated.\n";

        // Begin batch imports
        startBatchImport(importTasks);

        // Refresh project panel if anything changed
        if (anyChanges && panelProject != nullptr)
            panelProject->triggerRefresh();
    }

    // Reset so that it will check again
    showingMessageBox = false;
}

void Manager::refreshWindowTitle()
{
    if (!projectOpened)
    {
        window->setWindowTitle(initialWindowTitle);
    }
    else
    {
        if (worldName == "")
            window->setWindowTitle(initialWindowTitle + " - " + projectName + " - Untitled");
        else
            window->setWindowTitle(initialWindowTitle + " - " + projectName + " - " + worldName + ".world");

        // While the game panel is running, scene edits are temporary (reverted
        // on Stop) and must not mark the window title dirty.
        if (!projectSaved && !(panelGame && panelGame->getPlayState() != GamePlayState::Stopped))
            window->setWindowTitle(window->getWindowTitle() + "*");
    }
}

void Manager::closeEditor()
{
    if (!projectSaved)
    {
        showingMessageBox = true;

        auto result = pfd::message(
                          "Unsaved Changes",
                          "Project not saved. Do you really want to quit?",
                          pfd::choice::yes_no,
                          pfd::icon::warning)
                          .result();

        if (result == pfd::button::yes)
        {
            window->setRunning(false); // user confirmed quit
        }
    }
    else
    {
        window->setRunning(false);
    }
}

void Manager::clearWorld(bool forced)
{
    if (!forced)
    {
        if (projectOpened && worldName != "" && !projectSaved)
        {
            showingMessageBox = true;

            auto result = pfd::message(
                              "Unsaved Changes",
                              "World not saved. Do you really want to close this world?",
                              pfd::choice::yes_no,
                              pfd::icon::warning)
                              .result();

            if (result == pfd::button::no)
            {
                return;
            }
        }
    }

    if (!world->getScenes().empty())
    {
        for (kScene *scene : world->getScenes())
        {
            if (scene && scene->getRootNode())
            {
                for (kObject *child : scene->getRootNode()->getChildren())
                {
                    deleteObjectRecursive(child);
                }
                scene->getRootNode()->getChildren().clear();
                delete scene->getRootNode();
                delete scene;
            }
        }
        world->getScenes().clear();
    }

    // Clear terrain manager
    if (terrainManager)
    {
        terrainManager->clear();
    }

    // Drop stale Ingame UI (.ui) object links (their objects are gone).
    clearObjectUiAssets();
}

bool Manager::newWorld()
{
    // Ask user if they want to save unsaved changes
    if (!projectSaved)
    {
        showingMessageBox = true;
        auto result = pfd::message(
                          "Unsaved Changes",
                          "The current world has unsaved changes. Do you want to create a new world without saving?",
                          pfd::choice::yes_no,
                          pfd::icon::warning)
                          .result();
        showingMessageBox = false;
        if (result == pfd::button::no)
            return false;
    }

    clearWorld(true); // force clear, skip second prompt
    resetToFreshWorld();

    // Notify the renderer that the scene graph has been rebuilt.
    if (renderer)
        renderer->setOctreeDirty();

    return true;
}

void Manager::resetToFreshWorld()
{
    // Ensure the editor scene exists (clearWorld did not delete it)
    kScene *editorScene = nullptr;
    auto scenes = world->getScenes();
    if (scenes.empty())
    {
        editorScene = world->createScene("Editor Scene");
    }
    else
    {
        editorScene = scenes[0];
    }

    // Remove non-editor cameras from the world before deleting game scene
    // objects — otherwise the world's camera list would hold dangling pointers.
    {
        auto cams = world->getCameras();
        for (kCamera *c : cams)
            if (c != editorCamera)
                world->removeCamera(c);
    }

    // Remove any leftover game scenes
    for (size_t i = 1; i < world->getScenes().size(); ++i)
    {
        kScene *s = world->getScenes()[i];
        if (s && s->getRootNode())
        {
            for (kObject *child : s->getRootNode()->getChildren())
                deleteObjectRecursive(child);
            delete s->getRootNode();
        }
        world->removeScene(s);
        delete s;
    }

    // Create a fresh default game scene
    kScene *scene = world->createScene("Scene");
    scene->setActive(true);
    applyDefaultSkybox(scene);
    loadDefaultWorldInto(scene);
    setScene(scene);

    // Reset editor state
    selectedObject = nullptr;
    selectedScene = nullptr;
    worldSelected = false;
    selectedObjects.clear();
    defaultGameCamera = nullptr;
    worldName = "";
    worldPath.clear();
    projectSaved = false;
    undoRedo.clear();

    refreshWindowTitle();

    if (panelHierarchy)
        panelHierarchy->refreshList();

    std::cout << "New world created." << std::endl;
}

void Manager::deleteObjectRecursive(kObject *node)
{
    if (!node)
        return;

    // Delete all children first
    for (kObject *child : node->getChildren())
    {
        deleteObjectRecursive(child);
    }

    // Clear the children list to avoid dangling pointers
    node->getChildren().clear();

    // If this node is a camera, remove it from the world camera list
    // before deletion so that PanelGame::findGameCamera() won't return
    // a dangling pointer and crash (e.g. when applying a prefab).
    if (node->getType() == NODE_TYPE_CAMERA)
    {
        kCamera *cam = static_cast<kCamera *>(node);
        if (world)
            world->removeCamera(cam);
        if (defaultGameCamera == cam)
            defaultGameCamera = nullptr;
    }

    // Finally delete this node
    delete node;
}

kString Manager::checkAssetType(const fs::path &p)
{
    auto ext = p.extension().string();

    if (ext == ".txt" || ext == ".ini" || ext == ".xml" || ext == ".json")
        return "text";
    else if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".png" || ext == ".gif" || ext == ".tga")
        return "image";
    else if (ext == ".as")
        return "script";
    else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg")
        return "audio";
    else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".webm")
        return "video";
    else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".dae" || ext == ".stl")
        return "mesh";
    else if (ext == ".prefab" || ext == ".pfb")
        return "prefab";
    else if (ext == ".particle")
        return "particle";
    else if (ext == ".world")
        return "world";
    else if (ext == ".mat")
        return "material";
    else if (ext == ".glsl" || ext == ".hlsl")
        return "shader"; // hand-written raw shader (with `// @var` annotations)
    else if (ext == ".animator")
        return "animator";
    else if (ext == ".animation")
        return "animation";
    else if (ext == ".cinematic")
        return "animation";
    else if (ext == ".ui")
        return "ui";

    return "unknown"; // Unknown
}

void Manager::startBatchImport(const std::vector<ImportTask> &tasks)
{
    if (tasks.empty())
        return;

    {
        std::scoped_lock lock(queueMutex);
        importQueue = tasks;
    }

    filesProcessed = 0;
    batchDone = false;
    showImportPopup = true;

    importFuture = std::async(std::launch::async, [this]()
                              {
		for (auto& task : importQueue)
		{
			if (task.type == "mesh")
			{
				// Load the per-asset import settings (scale / tangents / animation)
				// from Library/Metadata/<uuid>.json so a re-import on another
				// machine honours the settings chosen in the inspector instead of
				// silently falling back to defaults.
				MeshImportOptions opt;
				fs::path metaPath = projectPath / "Library" / "Metadata" / (task.uuid + ".json");
				if (!task.uuid.empty() && fs::exists(metaPath))
				{
					try
					{
						std::ifstream mf(metaPath);
						nlohmann::json mj;
						mf >> mj;
						opt.scaleFactor = mj.value("scaleFactor", 1.0f);
						opt.tangents = mj.value("tangents", 0);
						opt.importAnimation = mj.value("importAnimation", true);
					}
					catch (...)
					{
					}
				}

				std::string err;
				task.success = convertMeshToGlbEx(task.inputPath, task.outputPath, opt, &err, &task.warnings);
				if (!task.success)
					task.errorMsg = err.empty() ? "mesh import failed" : err;
			}
			else if (task.type == "image")
			{
				task.success = convertImageToDxt5(task.inputPath, task.outputPath);
				if (!task.success)
					task.errorMsg = "image import failed (unsupported or corrupt source file?)";
			}
			else if (task.type == "audio")
			{
				task.success = convertAudioToWav(task.inputPath, task.outputPath);
				if (!task.success)
					task.errorMsg = "audio import failed (unsupported or corrupt source file?)";
			}
			else
			{
				// Not handled yet -> mark as skipped
				task.success = false;
				task.errorMsg = "asset type '" + task.type + "' not supported yet";
				std::cout << "Skipping: " << task.inputPath << " (type=" << task.type << " not supported yet)\n";
			}

			filesProcessed++;
		}
		batchDone = true; });
}

void Manager::drawImportPopup(PanelConsole *console)
{
    if (showImportPopup)
    {
        kGuiManager *gui = console->gui;
        kVec2 center = gui->getMainViewportCenter();
        gui->setNextWindowPos(center, ImGuiCond_Appearing, kVec2(0.5f, 0.5f));

        if (gui->popupModal("Importing Assets...", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            int totalFiles = (int)importQueue.size();
            int processed = filesProcessed.load();

            float progress = (totalFiles > 0) ? (float)processed / (float)totalFiles : 0.0f;

            if (!batchDone)
            {
                gui->text("Converting files...");
                gui->progressBar(progress, kVec2(250.f, 0.f));
                gui->text("Processed " + std::to_string(processed) + " / " + std::to_string(totalFiles));
                gui->popupEnd();
            }
            else
            {
                if (importEndTime.time_since_epoch().count() == 0)
                    importEndTime = std::chrono::steady_clock::now();

                gui->text("Import complete!");

                gui->progressBar(progress, kVec2(250.f, 0.f));
                gui->text("Processed " + std::to_string(processed) + " / " + std::to_string(totalFiles));

                for (auto &task : importQueue)
                {
                    // Non-fatal importer warnings (orange), logged once per task
                    // regardless of success/failure.
                    if (!task.warningsLogged)
                    {
                        for (const std::string &w : task.warnings)
                            console->addLog(LogLevel::Warning, "[Import] %s: %s",
                                            task.inputPath.generic_string().c_str(), w.c_str());
                        task.warningsLogged = true;
                    }

                    if (!task.success && !task.reported)
                    {
                        // Audio files play directly from source — WAV conversion is
                        // a best-effort optimisation, not a hard requirement.
                        LogLevel lvl = (task.type == "audio") ? LogLevel::Warning : LogLevel::Error;
                        console->addLog(lvl, "[Import] Failed to convert '%s': %s",
                                        task.inputPath.generic_string().c_str(),
                                        task.errorMsg.empty() ? "unknown error" : task.errorMsg.c_str());
                        task.reported = true;
                    }
                    else if (task.success && !task.reported && task.type == "mesh" && !task.thumbnailPath.empty())
                    {
                        thumbnailQueue.push_back({task.uuid, task.outputPath, task.thumbnailPath, "mesh"});
                        task.reported = true;
                    }
                    else if (task.success && !task.reported && task.type == "image" && !task.thumbnailPath.empty())
                    {
                        thumbnailQueue.push_back({task.uuid, task.inputPath, task.thumbnailPath, "image"});
                        task.reported = true;
                    }
                }

                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - importEndTime).count() >= 2)
                {
                    gui->closeCurrentPopup();
                    showImportPopup = false;
                    importEndTime = {};
                }

                gui->popupEnd();

                importTasks.clear();
            }
        }
    }
}

void Manager::processThumbnailQueue(PanelConsole *console)
{
    if (thumbnailQueue.empty())
        return;

    ThumbnailTask task = thumbnailQueue.front();
    thumbnailQueue.erase(thumbnailQueue.begin());

    // Skip if thumbnail already exists
    if (fs::exists(task.thumbnailPath))
        return;

    // Skip stale tasks whose source no longer exists (e.g. the asset was moved or
    // deleted after the task was queued). checkAssetChange re-queues with the
    // correct path when a thumbnail is actually missing, so dropping these is safe
    // — and avoids spurious "cannot open" errors in the console.
    if (!task.srcPath.empty() && !fs::exists(task.srcPath))
        return;

    if (task.type == "audio")
    {
        const int THUMB = 128;
        const uint8_t BG = 42;
        const uint8_t WAVE_COLOR_R = 80;
        const uint8_t WAVE_COLOR_G = 160;
        const uint8_t WAVE_COLOR_B = 220;

        std::vector<uint8_t> canvas(THUMB * THUMB * 4);
        for (int i = 0; i < THUMB * THUMB * 4; i += 4)
        {
            canvas[i] = BG;
            canvas[i + 1] = BG;
            canvas[i + 2] = BG;
            canvas[i + 3] = 255;
        }

        const int centerY = THUMB / 2;
        const int maxAmplitude = THUMB / 2 - 6;

        // Attempt to decode the actual audio file for a real waveform.
        // Use the same custom backends (libvorbis, libopus) as convertAudioToWav
        // and the audio preview engine so .ogg (Vorbis) and .opus files decode.
        float* pcmData = nullptr;
        ma_uint64 frameCount = 0;
        {
            ma_decoder_config decCfg = ma_decoder_config_init(ma_format_f32, 1, 0);

            ma_decoding_backend_vtable* customBackends[] = {
                ma_decoding_backend_libvorbis,
                ma_decoding_backend_libopus,
            };
            decCfg.ppCustomBackendVTables = customBackends;
            decCfg.customBackendCount     = sizeof(customBackends) / sizeof(customBackends[0]);
            decCfg.pCustomBackendUserData = nullptr;

            ma_result mr = ma_decode_file(task.srcPath.string().c_str(), &decCfg,
                                          &frameCount, (void**)&pcmData);
            if (mr != MA_SUCCESS)
            {
                pcmData = nullptr;
                frameCount = 0;
            }
        }

        std::vector<float> monoBuf;
        if (pcmData && frameCount > 0)
        {
            // Cap at ~60 seconds at 48 kHz to limit memory usage.
            const ma_uint64 capFrames = 48000 * 60;
            if (frameCount > capFrames)
                frameCount = capFrames;

            monoBuf.assign(pcmData, pcmData + (size_t)frameCount);
            ma_free(pcmData, NULL);
        }

        if (!monoBuf.empty())
        {
            // Draw one vertical bar per pixel column, derived from the actual samples.
            const int numBars = THUMB;
            size_t samplesPerBar = monoBuf.size() / (size_t)numBars;
            if (samplesPerBar < 1)
                samplesPerBar = 1;

            for (int bar = 0; bar < numBars; ++bar)
            {
                size_t start = (size_t)bar * samplesPerBar;
                size_t end = start + samplesPerBar;
                if (end > monoBuf.size())
                    end = monoBuf.size();

                // Peak amplitude within this slice.
                float peak = 0.0f;
                for (size_t s = start; s < end; ++s)
                {
                    float a = std::abs(monoBuf[s]);
                    if (a > peak) peak = a;
                }

                int barH = (int)(peak * (float)maxAmplitude);
                if (barH < 1) barH = 1;
                if (barH > maxAmplitude) barH = maxAmplitude;

                // Draw the vertical bar — brighter near centre, fading toward edges.
                for (int dy = -barH; dy <= barH; ++dy)
                {
                    int py = centerY + dy;
                    if (py < 0 || py >= THUMB)
                        continue;
                    int idx = (py * THUMB + bar) * 4;

                    float fade = 1.0f - (std::abs((float)dy) / (float)barH) * 0.45f;
                    canvas[idx]     = (uint8_t)((float)WAVE_COLOR_R * fade);
                    canvas[idx + 1] = (uint8_t)((float)WAVE_COLOR_G * fade);
                    canvas[idx + 2] = (uint8_t)((float)WAVE_COLOR_B * fade);
                    canvas[idx + 3] = 255;
                }
            }
        }
        else
        {
            // Fallback: a simple centred horizontal line when decoding fails.
            int idx = centerY * THUMB * 4;
            for (int x = 4; x < THUMB - 4; ++x)
            {
                int i = idx + x * 4;
                canvas[i]     = WAVE_COLOR_R;
                canvas[i + 1] = WAVE_COLOR_G;
                canvas[i + 2] = WAVE_COLOR_B;
                canvas[i + 3] = 180;
            }
        }

        stbi_write_png(task.thumbnailPath.string().c_str(), THUMB, THUMB, 4, canvas.data(), THUMB * 4);
        if (panelProject != nullptr)
            panelProject->triggerRefresh();
        return;
    }

    if (task.type == "image")
    {
        // Load source image, fit into 128x128 canvas, save as PNG
        const int THUMB = 128;
        const uint8_t BG = 42;

        int w, h, c;
        unsigned char *src = stbi_load(task.srcPath.string().c_str(), &w, &h, &c, 4);
        if (!src)
        {
            console->addLog(LogLevel::Error,
                            ("[Error] Thumbnail: failed to load image " + task.srcPath.generic_string()).c_str());
            return;
        }

        float scale = std::min((float)THUMB / w, (float)THUMB / h);
        int fitW = std::max(1, (int)(w * scale));
        int fitH = std::max(1, (int)(h * scale));

        std::vector<uint8_t> resized(fitW * fitH * 4);
        stbir_resize_uint8_linear(src, w, h, 0, resized.data(), fitW, fitH, 0, STBIR_RGBA);
        stbi_image_free(src);

        // Fill canvas with background, then blit the resized image centered
        std::vector<uint8_t> canvas(THUMB * THUMB * 4);
        for (int i = 0; i < THUMB * THUMB * 4; i += 4)
        {
            canvas[i] = BG;
            canvas[i + 1] = BG;
            canvas[i + 2] = BG;
            canvas[i + 3] = 255;
        }
        int offX = (THUMB - fitW) / 2;
        int offY = (THUMB - fitH) / 2;
        for (int y = 0; y < fitH; y++)
            for (int x = 0; x < fitW; x++)
            {
                int s = (y * fitW + x) * 4;
                int d = ((offY + y) * THUMB + (offX + x)) * 4;
                canvas[d] = resized[s];
                canvas[d + 1] = resized[s + 1];
                canvas[d + 2] = resized[s + 2];
                canvas[d + 3] = resized[s + 3];
            }

        bool saved = stbi_write_png(task.thumbnailPath.string().c_str(),
                                    THUMB, THUMB, 4, canvas.data(), THUMB * 4) != 0;
        if (!saved)
            console->addLog(LogLevel::Error,
                            ("[Error] Thumbnail: failed to save " + task.thumbnailPath.generic_string()).c_str());
        else if (panelProject != nullptr)
            panelProject->triggerRefresh();

        return;
    }

    // Material thumbnail — render a sphere with the material's shader
    if (task.type == "material")
    {
        static const char *kPinkVS = R"(
			#version 330 core
			layout(location = 0) in vec3 aPosition;
			uniform mat4 modelMatrix;
			uniform mat4 viewMatrix;
			uniform mat4 projectionMatrix;
			void main() {
				gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(aPosition, 1.0);
			}
		)";
        static const char *kPinkFS = R"(
			#version 330 core
			out vec4 fragColor;
			void main() { fragColor = vec4(1.0, 0.0, 1.0, 1.0); }
		)";

        nlohmann::json matJson;
        {
            std::ifstream f(task.srcPath);
            if (!f.is_open())
            {
                console->addLog(LogLevel::Error,
                                ("[Error] Material thumbnail: cannot open " + task.srcPath.generic_string()).c_str());
                return;
            }
            try
            {
                f >> matJson;
            }
            catch (...)
            {
                console->addLog(LogLevel::Error,
                                ("[Error] Material thumbnail: invalid JSON in " + task.srcPath.generic_string()).c_str());
                return;
            }
        }

        kMesh *sphere = kMeshGenerator::generateSphere(1.0f, 32, 32);
        if (!sphere)
            return;
        sphere->calculateModelMatrix();

        // Build the material exactly like the inspector preview / scene does, so
        // the thumbnail reflects the @var parameters. Falls back to a pink error
        // shader if the material's shader can't be built.
        kMaterial *mat = buildMaterialFromJson(matJson);
        if (!mat)
        {
            static kShader *s_shaderPink = nullptr;
            if (!s_shaderPink)
            {
                s_shaderPink = new kShader();
                s_shaderPink->loadShadersCode(kPinkVS, kPinkFS);
            }
            mat = new kMaterial();
            mat->setShader(s_shaderPink);
        }

        sphere->setMaterial(mat);

        bool saved = false;
        try
        {
            thumbnailRenderer.setBackgroundColor(kVec4(42 / 255.0f, 42 / 255.0f, 42 / 255.0f, 1.0f));
            thumbnailRenderer.renderMeshWithMaterial(sphere);
            saved = thumbnailRenderer.saveToFile(task.thumbnailPath.generic_string());
        }
        catch (...)
        {
            saved = false;
        }

        // Neither kMesh nor kMaterial take ownership, delete each separately
        // shader is cached static — not deleted here
        delete sphere;
        delete mat;

        if (!saved)
            console->addLog(LogLevel::Error,
                            ("[Error] Material thumbnail: failed to save " + task.thumbnailPath.generic_string()).c_str());
        else if (panelProject != nullptr)
            panelProject->triggerRefresh();
        return;
    }

    // Mesh thumbnail — rendered via offscreen renderer
    kAssetManager *am = getAssetManager();
    if (!am)
        return;
    kMesh *mesh = am->loadMesh(task.srcPath.generic_string());
    if (!mesh)
    {
        console->addLog(LogLevel::Error,
                        ("[Error] Thumbnail: failed to load mesh " + task.srcPath.generic_string()).c_str());
        return;
    }

    mesh->calculateModelMatrix();

    bool saved = false;
    try
    {
        thumbnailRenderer.setBackgroundColor(kVec4(42 / 255.0f, 42 / 255.0f, 42 / 255.0f, 1.0f));
        thumbnailRenderer.renderMesh(mesh);
        saved = thumbnailRenderer.saveToFile(task.thumbnailPath.generic_string());
    }
    catch (...)
    {
        saved = false;
    }

    if (!saved)
        console->addLog(LogLevel::Error,
                        ("[Error] Thumbnail: failed to save " + task.thumbnailPath.generic_string()).c_str());
    else if (panelProject != nullptr)
        panelProject->triggerRefresh();
    // Mesh is owned by the asset manager — do not delete manually.
}

void Manager::selectObject(const kString uuid, bool clearList)
{
    selectedScene = nullptr;
    worldSelected = false;

    if (clearList)
        selectedObjects.clear();

    auto it = std::find(selectedObjects.begin(), selectedObjects.end(), uuid);
    if (it == selectedObjects.end())
    {
        selectedObjects.push_back(uuid);
    }
}

void Manager::deselectObject(const kString uuid)
{
    auto it = std::find(selectedObjects.begin(), selectedObjects.end(), uuid);
    if (it != selectedObjects.end())
    {
        selectedObjects.erase(it);
    }
}

kObject *Manager::findObjectByUuid(const kString &uuid)
{
    // Fast path: objectMap (populated by hierarchy panel)
    auto it = objectMap.find(uuid);
    if (it != objectMap.end() && it->second.object)
        return it->second.object;

    // Search helper: recursively find a kObject by UUID in a scene graph.
    auto findInScene = [](kScene *s, const kString &uuid) -> kObject *
    {
        if (!s || !s->getRootNode())
            return nullptr;
        std::function<kObject *(kObject *)> find = [&](kObject *node) -> kObject *
        {
            for (kObject *child : node->getChildren())
            {
                if (child->getUuid() == uuid)
                    return child;
                if (kObject *hit = find(child))
                    return hit;
            }
            return nullptr;
        };
        return find(s->getRootNode());
    };

    // Fallback: traverse the game scene graph.
    if (kObject *found = findInScene(scene, uuid))
        return found;

    // Also search the prefab scene when prefab editing is active (objects
    // in the prefab world are separate from the game world's scene graph).
    if (prefabEditing && prefabScene)
        return findInScene(prefabScene, uuid);

    return nullptr;
}

std::vector<TransformState> Manager::captureSelectedTransforms()
{
    std::vector<TransformState> states;
    const auto &sel = getActiveSelectedObjects();
    for (const auto &uuid : sel)
    {
        kObject *obj = findObjectByUuid(uuid);
        if (!obj)
            continue;
        states.push_back({uuid, obj->getPosition(), obj->getRotation(), obj->getScale()});
    }
    return states;
}

// ---------------------------------------------------------------------------
// Ingame UI (.ui) object registry
//
// The engine's kObject has no generic metadata slot, so the mapping from a
// scene object to the .ui asset it displays lives here (not on the object).
// It is round-tripped through the world file as an extra "ui" key per object.
// ---------------------------------------------------------------------------
static std::unordered_map<kString, kString> g_objectUiAssets; // object uuid -> .ui asset uuid

// Injects the "ui" key into every serialized object node that has a registered
// .ui reference, so the link survives save/load, duplication and undo snapshots.
//
// NOTE: the walk must visit *every* nested value, not just a node's "children"
// array — a serialized world stores objects under scenes[].objects[], which is
// an ordinary keyed array, so descending only through "children" would miss them
// and the .ui link would be lost on save/reload.
static void injectUiRefsIntoJson(nlohmann::json &node)
{
    if (node.is_array())
    {
        for (auto &child : node)
            injectUiRefsIntoJson(child);
        return;
    }
    if (!node.is_object())
        return;

    if (node.contains("uuid") && node["uuid"].is_string())
    {
        kString uuid = node["uuid"].get<std::string>();
        auto it = g_objectUiAssets.find(uuid);
        if (it != g_objectUiAssets.end() && !it->second.empty())
            node["ui"] = it->second;
    }

    for (auto it = node.begin(); it != node.end(); ++it)
        injectUiRefsIntoJson(it.value());
}

// Forward decls — defined further down in this file but needed by
// duplicateSelectedObjects below.
static kObject *loadObjectFromJson(const nlohmann::json &obj, kScene *scene, kWorld *world,
                                   kAssetManager *am, const fs::path &projectPath,
                                   kCamera *editorCamera, kObject *parent = nullptr,
                                   kTerrainManager *terrainMgr = nullptr);
static void pushInstantiateCommand(Manager *mgr, kObject *root);
static void reapplyMaterialsRecursive(Manager *mgr, kObject *node, const fs::path &projectPath);
static void assignImportChildUuidsRec(kObject *root);

// Walk a serialized kObject JSON tree and replace every UUID with a fresh one
// so the cloned subtree doesn't collide with its original. Recurses into the
// "children" array and also bumps the per-component UUIDs that kObject emits
// (scripts, particles, audio sources / listeners) so attachments stay unique.
static void regenerateUuidsRecursive(nlohmann::json &j)
{
    if (!j.is_object())
        return;

    if (j.contains("uuid") && j["uuid"].is_string())
        j["uuid"] = generateUuid();

    for (const char *key : {"script", "scripts", "particles",
                            "audio_sources", "audio_listeners"})
    {
        if (j.contains(key) && j[key].is_array())
            for (auto &item : j[key])
                if (item.is_object() && item.contains("uuid") && item["uuid"].is_string())
                    item["uuid"] = generateUuid();
    }

    if (j.contains("children") && j["children"].is_array())
        for (auto &c : j["children"])
            regenerateUuidsRecursive(c);
}

void Manager::duplicateSelectedObjects()
{
    kScene *s = getCreationScene();
    if (!s || selectedObjects.empty())
        return;

    kAssetManager *am = getAssetManager();
    kWorld *w = getCreationWorld();

    // Snapshot the current selection — loadObjectFromJson + pushInstantiateCommand
    // will rewrite selectedObjects below, so we can't iterate the live vector.
    std::vector<kString> sourceUuids = selectedObjects;
    std::vector<kObject *> duplicates;
    duplicates.reserve(sourceUuids.size());

    for (const auto &uuid : sourceUuids)
    {
        kObject *src = findObjectByUuid(uuid);
        if (!src)
            continue;

        nlohmann::json j = src->serialize();
        regenerateUuidsRecursive(j);
        injectUiRefsIntoJson(j); // carry the .ui link onto the duplicate

        kObject *clone = loadObjectFromJson(j, s, w, am,
                                            projectPath, editorCamera, nullptr);
        if (clone)
        {
            // Rebuild the runtime material from the cloned material UUID so the
            // duplicate looks identical immediately (not only after a reload).
            reapplyMaterialsRecursive(this, clone, projectPath);
            duplicates.push_back(clone);
        }
    }

    if (duplicates.empty())
        return;

    // Make the new objects the active selection.
    selectedObjects.clear();
    for (kObject *obj : duplicates)
        selectedObjects.push_back(obj->getUuid());
    selectedObject = duplicates.back();

    if (panelHierarchy)
        panelHierarchy->refreshList();

    projectSaved = false;
    refreshWindowTitle();

    // One undo step per spawned root — mirrors how individual createMesh /
    // createLight / etc. push their own InstantiateCommand. Ctrl+Z undoes the
    // duplicates one at a time, in reverse spawn order.
    for (kObject *obj : duplicates)
        pushInstantiateCommand(this, obj);
}

void Manager::deleteSelectedObjects()
{
    kScene *s = getCreationScene();
    if (!s || selectedObjects.empty())
        return;

    std::vector<DeletedObjectInfo> deleted;

    for (const auto &uuid : selectedObjects)
    {
        kObject *obj = findObjectByUuid(uuid);
        if (!obj)
            continue;

        kNodeType type = obj->getType();

        if (type == NODE_TYPE_LIGHT)
            s->removeLight(static_cast<kLight *>(obj));
        else
            s->removeMesh(static_cast<kMesh *>(obj));

        deleted.push_back({obj, type, s});
    }

    if (deleted.empty())
        return;

    auto cmd = std::make_unique<DeleteCommand>(
        this, std::move(deleted), selectedObjects, selectedObject);

    selectedObjects.clear();
    selectedObject = nullptr;

    if (panelHierarchy)
        panelHierarchy->refreshList();

    undoRedo.push(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Recursively collect all UUIDs reachable under node (excluding the node itself).
static void collectUuids(kObject *node, std::vector<kString> &out, kObject **firstObj)
{
    for (kObject *child : node->getChildren())
    {
        out.push_back(child->getUuid());
        if (*firstObj == nullptr)
            *firstObj = child;
        collectUuids(child, out, firstObj);
    }
}

// Apply a Phong material to a mesh.
static void applyDefaultMaterial(kMesh *mesh, kAssetManager *am)
{
    kShader *shader = am->loadGlslFromResource("SHADER_MESH_PHONG");
    kMaterial *mat = am->createMaterial(shader);
    mat->setAmbientColor(kVec3(1.0f, 1.0f, 1.0f));
    mat->setDiffuseColor(kVec3(0.5f, 0.5f, 0.5f));
    mesh->setMaterial(mat);
}

// Apply a gizmo icon material to a light.
static void applyLightIcon(kLight *light, kAssetManager *am, const char *gizmoResource)
{
    kShader *shader = am->loadGlslFromResource("SHADER_ICON");
    kMaterial *mat = am->createMaterial(shader);
    mat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND); // blend so the icon's drop shadow / soft edges show
    kTexture2D *tex = am->loadTexture2DFromResource(gizmoResource, "albedoMap",
                                                    kTextureFormat::TEX_FORMAT_RGBA);
    mat->addTexture(tex);
    light->setMaterial(mat);
}

// Apply the GIZMO_CAMERA icon material to a camera so it renders as a
// billboard in the editor view (skipped automatically by the renderer when
// the camera being drawn is also the active view camera).
static void applyCameraIcon(kCamera *cam, kAssetManager *am)
{
    kShader *shader = am->loadGlslFromResource("SHADER_ICON");
    kMaterial *mat = am->createMaterial(shader);
    mat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND); // blend so the icon's drop shadow / soft edges show
    kTexture2D *tex = am->loadTexture2DFromResource("GIZMO_CAMERA", "albedoMap",
                                                    kTextureFormat::TEX_FORMAT_RGBA);
    mat->addTexture(tex);
    cam->setMaterial(mat);
}

// Apply the GIZMO_AUDIO icon material to an audio-source object.
static void applyAudioIcon(kObject *obj, kAssetManager *am)
{
    kShader *shader = am->loadGlslFromResource("SHADER_ICON");
    kMaterial *mat = am->createMaterial(shader);
    mat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND);
    kTexture2D *tex = am->loadTexture2DFromResource("GIZMO_AUDIO", "albedoMap",
                                                    kTextureFormat::TEX_FORMAT_RGBA);
    mat->addTexture(tex);
    obj->setMaterial(mat);
}

// Apply the GIZMO_LISTENER icon material to an audio-listener object.
static void applyAudioListenerIcon(kObject *obj, kAssetManager *am)
{
    kShader *shader = am->loadGlslFromResource("SHADER_ICON");
    kMaterial *mat = am->createMaterial(shader);
    mat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND);
    kTexture2D *tex = am->loadTexture2DFromResource("GIZMO_LISTENER", "albedoMap",
                                                    kTextureFormat::TEX_FORMAT_RGBA);
    mat->addTexture(tex);
    obj->setMaterial(mat);
}

// ---------------------------------------------------------------------------
// Edit — selection helpers
// ---------------------------------------------------------------------------

void Manager::selectAll()
{
    if (!scene)
        return;

    std::vector<kString> newSel;
    kObject *newSelObj = nullptr;
    collectUuids(scene->getRootNode(), newSel, &newSelObj);

    if (newSel == selectedObjects)
        return;

    auto before = selectedObjects;
    auto beforeObj = selectedObject;

    selectedObjects = newSel;
    selectedObject = newSelObj;

    undoRedo.push(std::make_unique<SelectCommand>(this, before, beforeObj, newSel, newSelObj));
}

void Manager::deselectAll()
{
    if (selectedObjects.empty())
        return;

    auto before = selectedObjects;
    auto beforeObj = selectedObject;

    selectedObjects.clear();
    selectedObject = nullptr;

    undoRedo.push(std::make_unique<SelectCommand>(this, before, beforeObj, std::vector<kString>{}, nullptr));
}

void Manager::invertSelection()
{
    if (!scene)
        return;

    std::set<kString> currentSet(selectedObjects.begin(), selectedObjects.end());

    std::vector<kString> allUuids;
    kObject *dummy = nullptr;
    collectUuids(scene->getRootNode(), allUuids, &dummy);

    std::vector<kString> newSel;
    kObject *newSelObj = nullptr;
    for (const auto &uuid : allUuids)
    {
        if (currentSet.find(uuid) == currentSet.end())
        {
            newSel.push_back(uuid);
            if (newSelObj == nullptr)
                newSelObj = findObjectByUuid(uuid);
        }
    }

    auto before = selectedObjects;
    auto beforeObj = selectedObject;

    selectedObjects = newSel;
    selectedObject = newSelObj;

    undoRedo.push(std::make_unique<SelectCommand>(this, before, beforeObj, newSel, newSelObj));
}

// ---------------------------------------------------------------------------
// Object creation — shared post-creation logic
// ---------------------------------------------------------------------------

static void finishCreate(Manager *mgr, kObject *obj, kScene *scene,
                         std::function<void()> undoFn, std::function<void()> redoFn)
{
    kString uuid = obj->getUuid();
    mgr->selectedObject = obj;
    mgr->selectObject(uuid, true);

    if (mgr->panelHierarchy)
        mgr->panelHierarchy->refreshList();

    mgr->undoRedo.push(std::make_unique<PropertyCommand>(
        std::move(undoFn), std::move(redoFn)));
}

// ---------------------------------------------------------------------------
// Create Scene
// ---------------------------------------------------------------------------

void Manager::createSceneObject()
{
    kWorld *w = getCreationWorld();
    kScene *newScene = w->createScene("New Scene");

    undoRedo.push(std::make_unique<PropertyCommand>(
        [this, w, newScene]()
        { w->removeScene(newScene); },
        [this, w, newScene]()
        { w->addScene(newScene); }));
}

// ---------------------------------------------------------------------------
// Create Empty
// ---------------------------------------------------------------------------

void Manager::createEmpty()
{
    kScene *s = getCreationScene();
    if (!s)
        return;

    kObject *obj = new kObject();
    obj->setName("Empty");
    s->addObject(obj);
    kString uuid = obj->getUuid();

    finishCreate(this, obj, s, [this, s, obj, uuid]()
                 {
            s->removeObject(obj);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == obj) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, s, obj, uuid]()
                 {
            s->addObject(obj, uuid);
            selectedObject = obj;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

// ---------------------------------------------------------------------------
// Primitive auto-physics — when a procedural primitive (cube / sphere /
// capsule / cylinder / plane) is created we attach a physics descriptor whose
// shape and size match the generated mesh, so the object already behaves as a
// rigid body / collider when the scene is played, without the user having to
// add a physics component manually.
// ---------------------------------------------------------------------------
static void configurePrimitivePhysics(kMesh *mesh)
{
    const kString type = mesh->getPrimitiveType();
    if (type.empty())
        return;

    // Compute the object-space bounds from the generated vertices so the
    // collider always matches the actual geometry (works for any generator
    // size argument, not just the defaults).
    const std::vector<kVec3> &verts = mesh->getVerticesRef();
    if (verts.empty())
        return;

    float minX = verts[0].x, minY = verts[0].y, minZ = verts[0].z;
    float maxX = verts[0].x, maxY = verts[0].y, maxZ = verts[0].z;
    for (const kVec3 &v : verts)
    {
        minX = std::min(minX, v.x);
        minY = std::min(minY, v.y);
        minZ = std::min(minZ, v.z);
        maxX = std::max(maxX, v.x);
        maxY = std::max(maxY, v.y);
        maxZ = std::max(maxZ, v.z);
    }
    const float extX = std::max(0.0f, (maxX - minX) * 0.5f);
    const float extY = std::max(0.0f, (maxY - minY) * 0.5f);
    const float extZ = std::max(0.0f, (maxZ - minZ) * 0.5f);
    const float radius = std::max(extX, extZ);

    kPhysicsObjectDesc &desc = mesh->getPhysicsDesc();
    // Solid primitives spawn dynamic so they fall and collide at play time.
    desc.type = kPhysicsObjectType::Dynamic;
    desc.mass = 1.0f;

    if (type == "cube")
    {
        desc.shape.type        = kPhysicsShapeType::Box;
        desc.shape.halfExtents = kVec3(extX, extY, extZ);
    }
    else if (type == "sphere")
    {
        desc.shape.type   = kPhysicsShapeType::Sphere;
        desc.shape.radius = radius;
    }
    else if (type == "cylinder")
    {
        desc.shape.type   = kPhysicsShapeType::Cylinder;
        desc.shape.radius = radius;
        desc.shape.height = extY * 2.0f; // total height
    }
    else if (type == "capsule")
    {
        desc.shape.type = kPhysicsShapeType::Capsule;
        desc.shape.radius = radius;
        // Descriptor height is the cylindrical-segment length: the mesh's
        // total tip-to-tip height (2*extY) minus its two hemispherical caps
        // (2*radius), matching how kPhysicsObject builds the Jolt capsule.
        desc.shape.height = std::max(0.001f, extY * 2.0f - 2.0f * radius);
    }
    else if (type == "plane")
    {
        desc.shape.type        = kPhysicsShapeType::Plane;
        desc.type              = kPhysicsObjectType::Static; // Jolt planes are static-only
        desc.shape.halfExtents = kVec3(extX, extY, extZ);    // x/z drive the broadphase rect
    }
    else
    {
        return; // Unknown marker — leave the object without a physics body.
    }

    mesh->setHasPhysicsDesc(true);
}

// ---------------------------------------------------------------------------
// Create Mesh Primitive
// ---------------------------------------------------------------------------

void Manager::createMeshPrimitive(kMesh *mesh, const kString &name)
{
    kScene *s = getCreationScene();
    if (!s)
        return;

    kAssetManager *am = getAssetManager();
    if (am)
        applyDefaultMaterial(mesh, am);

    mesh->setName(name);

    // New primitives get a matching physics body automatically (shape + size).
    configurePrimitivePhysics(mesh);

    s->addMesh(mesh);
    kString uuid = mesh->getUuid();

    finishCreate(this, mesh, s, [this, s, mesh, uuid]()
                 {
            s->removeMesh(mesh);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == mesh) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, s, mesh, uuid]()
                 {
            s->addMesh(mesh, uuid);
            selectedObject = mesh;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

// ---------------------------------------------------------------------------
// Create Mesh from file
// ---------------------------------------------------------------------------

void Manager::createMeshFromFile()
{
    kScene *s = getCreationScene();
    if (!s)
        return;

    auto result = pfd::open_file("Open Mesh", "",
                                 {"Mesh files", "*.obj *.fbx *.gltf *.glb",
                                  "All files", "*"})
                      .result();

    if (result.empty())
        return;

    kString filePath = result[0];
    kAssetManager *am = getAssetManager();
    if (!am)
        return;

    kMesh *mesh = am->loadMesh(filePath);
    if (!mesh)
        return;

    mesh->setLoaded(true);
    mesh->setFileName(filePath);

    // Derive a display name from the filename
    fs::path p(filePath);
    mesh->setName(p.stem().string());

    if (am)
        applyDefaultMaterial(mesh, am);
    assignImportChildUuidsRec(mesh);

    scene->addMesh(mesh);
    kString uuid = mesh->getUuid();

    finishCreate(this, mesh, scene, [this, mesh, uuid]()
                 {
            scene->removeMesh(mesh);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == mesh) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, mesh, uuid]()
                 {
            scene->addMesh(mesh, uuid);
            selectedObject = mesh;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

// ---------------------------------------------------------------------------
// Create Light
// ---------------------------------------------------------------------------

void Manager::createLight(kLightType type)
{
    kScene *s = getCreationScene();
    if (!s)
        return;

    kAssetManager *am = getAssetManager();

    kLight *light = nullptr;
    const char *gizmoRes = "GIZMO_SUN_LIGHT";

    if (type == LIGHT_TYPE_SUN)
    {
        light = s->addSunLight(kVec3(0, 3, 0), kVec3(0, -1, 0),
                               kVec3(1.0f, 1.0f, 1.0f),
                               kVec3(1.0f, 1.0f, 1.0f));
        light->setName("Sun Light");
        gizmoRes = "GIZMO_SUN_LIGHT";
    }
    else if (type == LIGHT_TYPE_POINT)
    {
        light = s->addPointLight(kVec3(0, 2, 0),
                                 kVec3(1.0f, 1.0f, 1.0f),
                                 kVec3(1.0f, 1.0f, 1.0f));
        light->setName("Point Light");
        gizmoRes = "GIZMO_POINT_LIGHT";
    }
    else if (type == LIGHT_TYPE_SPOT)
    {
        light = s->addSpotLight(kVec3(0, 3, 0),
                                kVec3(1.0f, 1.0f, 1.0f),
                                kVec3(1.0f, 1.0f, 1.0f));
        light->setName("Spot Light");
        gizmoRes = "GIZMO_SPOT_LIGHT";
    }
    else
        return;

    light->setPower(1.0f);
    if (am)
        applyLightIcon(light, am, gizmoRes);

    kString uuid = light->getUuid();

    finishCreate(this, light, s, [this, s, light, uuid]()
                 {
            s->removeLight(light);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == light) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, s, light, uuid]()
                 {
            s->addLight(light);
            selectedObject = light;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

// ---------------------------------------------------------------------------
// Create Camera
// ---------------------------------------------------------------------------

void Manager::createCamera()
{
    kScene *s = getCreationScene();
    kWorld *w = getCreationWorld();
    if (!s)
        return;

    kCamera *cam = new kCamera();
    cam->setName("Camera");
    cam->setPosition(kVec3(0.0f, 1.0f, 5.0f));
    cam->setLookAt(kVec3(0.0f, 0.0f, 0.0f));
    cam->setFOV(60.0f);

    s->addObject(cam);
    w->addCamera(cam, cam->getUuid()); // also register in world camera list
    if (kAssetManager *am = getAssetManager())
        applyCameraIcon(cam, am);
    kString uuid = cam->getUuid();

    finishCreate(this, cam, s, [this, s, w, cam, uuid]()
                 {
            s->removeObject(cam);
            w->removeCamera(cam);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == cam) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, s, w, cam, uuid]()
                 {
            s->addObject(cam, uuid);
            w->addCamera(cam, uuid);
            selectedObject = cam;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

void Manager::createTerrain()
{
    if (!scene)
        return;

    // Lazily initialise the terrain manager
    if (!terrainManager)
    {
        terrainManager = new kTerrainManager();
        terrainManager->init(scene, getAssetManager(), 256.0f, 513);
    }

    kTerrain *tile = terrainManager->createTile(0, 0);
    if (tile)
    {
        tile->fillHeight(0.0f);
        tile->reload(true); // force mesh creation so it appears immediately

        kMesh *mesh = tile->getMesh();
        if (mesh)
        {
            selectedObject = mesh;
            selectObject(mesh->getUuid(), true);
        }

        if (renderer)
            renderer->setOctreeDirty();
        if (panelHierarchy)
            panelHierarchy->refreshList();

        projectSaved = false;
        refreshWindowTitle();
    }
}

void Manager::createNavMesh()
{
    if (!scene)
        return;

    // A navigation surface is a plain object carrying a nav-mesh component; the
    // object's transform also positions the optional area box.
    kObject *obj = new kObject();
    obj->setName("Nav Mesh");
    obj->setHasNavMeshDesc(true);
    scene->addObject(obj);
    kString uuid = obj->getUuid();

    finishCreate(this, obj, scene, [this, obj, uuid]()
                 {
            clearNavMesh(obj);
            scene->removeObject(obj);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == obj) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, obj, uuid]()
                 {
            scene->addObject(obj, uuid);
            selectedObject = obj;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

void Manager::createAudio()
{
    if (!scene)
        return;

    kObject *obj = new kObject();
    obj->setType(kNodeType::NODE_TYPE_AUDIO);
    obj->setName("Audio Source");
    scene->addObject(obj);
    kString uuid = obj->getUuid();

    // Attach a default audio source descriptor
    kAudioSource src;
    src.uuid = generateUuid();
    src.name = "Audio Source";
    src.isActive = true;
    src.playOnAwake = false;
    src.loop = false;
    src.volume = 1.0f;
    src.pitch = 1.0f;
    src.spatialize = true;
    obj->addAudioSource(src);

    // Assign the gizmo material for audio icon billboard
    if (kAssetManager *am = getAssetManager())
        applyAudioIcon(obj, am);

    finishCreate(this, obj, scene, [this, obj, uuid]()
                 {
            scene->removeObject(obj);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == obj) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, obj, uuid]()
                 {
            scene->addObject(obj, uuid);
            selectedObject = obj;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

void Manager::createAudioListener()
{
    if (!scene)
        return;

    kObject *obj = new kObject();
    obj->setType(kNodeType::NODE_TYPE_AUDIO);
    obj->setName("Audio Listener");
    scene->addObject(obj);
    kString uuid = obj->getUuid();

    // Attach a default audio listener descriptor
    kAudioListener listener;
    listener.uuid     = generateUuid();
    listener.isActive = true;
    obj->addAudioListener(listener);

    // Assign the gizmo material for listener icon billboard
    if (kAssetManager *am = getAssetManager())
        applyAudioListenerIcon(obj, am);

    finishCreate(this, obj, scene, [this, obj, uuid]()
                 {
            scene->removeObject(obj);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == obj) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, obj, uuid]()
                 {
            scene->addObject(obj, uuid);
            selectedObject = obj;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

void Manager::createParticle()
{
    if (!scene)
        return;

    kObject *obj = new kObject();
    obj->setName("Particle System");
    scene->addObject(obj);
    kString uuid = obj->getUuid();

    // Attach a default particle descriptor
    kParticle p;
    p.uuid = generateUuid();
    p.name = "Particle System";
    p.isActive = true;
    p.looping = true;
    obj->addParticle(p);

    finishCreate(this, obj, scene, [this, obj, uuid]()
                 {
            scene->removeObject(obj);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == obj) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, obj, uuid]()
                 {
            scene->addObject(obj, uuid);
            selectedObject = obj;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

// ---------------------------------------------------------------------------
// Decal helpers
// ---------------------------------------------------------------------------

// Build a fresh material for a decal from one of the built-in shaders.
//   "flat"  -> Unlit (not affected by scene lighting)  [SHADER_MESH_FLAT]
//   "pbr"   -> Physically-based (lit)                  [SHADER_MESH_PBR]
//   "phong" -> Phong (lit)                             [SHADER_MESH_PHONG]
static void applyDecalShaderMaterial(kDecal *decal, kAssetManager *am,
                                     const kString &type)
{
    if (!decal || !am)
        return;

    const char *res = "SHADER_MESH_FLAT";
    if (type == "pbr")
        res = "SHADER_MESH_PBR";
    else if (type == "phong")
        res = "SHADER_MESH_PHONG";

    kShader *shader = am->loadGlslFromResource(res);
    if (!shader)
        return;

    kMaterial *mat = am->createMaterial(shader);
    mat->setAmbientColor(kVec3(1.0f, 1.0f, 1.0f));
    mat->setDiffuseColor(kVec3(1.0f, 1.0f, 1.0f));
    mat->setSpecularColor(kVec3(1.0f, 1.0f, 1.0f));
    mat->setShininess(32.0f);
    mat->setMetallic(0.0f);
    mat->setRoughness(0.6f);
    // Stickers are alpha-blended so RGBA artwork composites over the surface.
    mat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND);

    decal->setMaterial(mat);
    decal->setMaterialUuid("");
    decal->setShaderType(type);
}

bool Manager::applyDecalShaderType(kDecal *decal, const kString &type)
{
    if (!decal)
        return false;
    if (type != "flat" && type != "pbr" && type != "phong")
        return false;
    applyDecalShaderMaterial(decal, getAssetManager(), type);
    projectSaved = false;
    refreshWindowTitle();
    return true;
}

// ---------------------------------------------------------------------------
// Create Decal
// ---------------------------------------------------------------------------

void Manager::createDecal()
{
    kScene *s = getCreationScene();
    if (!s)
        return;

    kDecal *decal = new kDecal();
    decal->setName("Decal");
    s->addObject(decal);
    kString uuid = decal->getUuid();

    // Give the decal a flat, unlit, alpha-blended default material so it shows
    // as a plain white sticker immediately. The user can later switch the shader
    // type (flat/pbr/phong) or assign a .mat whose albedo map carries the decal
    // artwork.
    if (kAssetManager *am = getAssetManager())
        applyDecalShaderMaterial(decal, am, decal->getShaderType());

    finishCreate(this, decal, s, [this, s, decal, uuid]()
                 {
            s->removeObject(decal);
            selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(), uuid),
                                  selectedObjects.end());
            if (selectedObject == decal) selectedObject = nullptr;
            if (panelHierarchy) panelHierarchy->refreshList(); }, [this, s, decal, uuid]()
                 {
            s->addObject(decal, uuid);
            selectedObject = decal;
            selectObject(uuid, true);
            if (panelHierarchy) panelHierarchy->refreshList(); });
}

fs::path Manager::resolveAudioFileByUuid(const kString &audioUuid)
{
    if (audioUuid.empty() || projectPath.empty())
        return {};

    static const char *kAudioExts[] = {".wav", ".ogg", ".mp3", ".flac"};

    // 1) Converted copy under Library/ImportedAssets.
    for (const char *ext : kAudioExts)
    {
        fs::path cand = projectPath / "Library" / "ImportedAssets" / (audioUuid + ext);
        if (fs::exists(cand))
            return cand;
    }

    // 2) Live registry (assets.json -> Assets/<relative path>).
    auto it = fileMap.find(audioUuid);
    if (it != fileMap.end() && it->second.type == "audio")
    {
        fs::path srcPath = projectPath / "Assets" / it->second.path;
        if (fs::exists(srcPath))
            return srcPath;
    }

    // 3) Stale-UUID recovery via Library/Metadata/<uuid>.json. When an audio
    //    asset was re-imported with a new UUID while a scene still references
    //    the old one, assets.json no longer contains the old UUID, but the old
    //    metadata file still records the original source path.
    fs::path metaPath = projectPath / "Library" / "Metadata" / (audioUuid + ".json");
    if (fs::exists(metaPath))
    {
        try
        {
            json meta;
            std::ifstream in(metaPath);
            if (in)
            {
                in >> meta;
                if (meta.is_object() && meta.value("type", std::string("")) == "audio")
                {
                    fs::path srcDir  = meta.value("src_path", std::string(""));
                    fs::path srcFile = meta.value("src_file", std::string(""));
                    if (!srcDir.empty() && !srcFile.empty())
                    {
                        fs::path srcPath = projectPath / srcDir / srcFile;
                        if (fs::exists(srcPath))
                            return srcPath;
                    }
                }
            }
        }
        catch (...)
        {
        }
    }

    return {};
}

void Manager::startAudioPreview(kAudioSource &src)
{
    // If already previewing this same source, stop it (toggle off).
    if (audioPreviewClip && audioPreviewSourceUuid == src.uuid)
    {
        stopAudioPreview();
        return;
    }

    // If a different clip is playing, stop it first.
    if (audioPreviewClip)
        stopAudioPreview();

    // Resolve the stored audio-file UUID to an actual filesystem path.
    fs::path audioPath = resolveAudioFileByUuid(src.audioFile);

    if (audioPath.empty() || !fs::exists(audioPath))
    {
        std::cerr << "Audio preview: cannot resolve audio file for UUID " << src.audioFile << "\n";
        return;
    }

    if (!audioPreviewManager)
    {
        audioPreviewManager = createAudioManager();
        if (!audioPreviewManager || !audioPreviewManager->init())
        {
            std::cerr << "Audio preview: failed to init audio manager\n";
            audioPreviewManager = nullptr;
            return;
        }
        // Set up a default listener so non-spatialized preview sounds are audible.
        audioPreviewManager->setListenerPosition(kVec3(0.0f));
        audioPreviewManager->setListenerDirection(kVec3(0.0f, 0.0f, -1.0f), kVec3(0.0f, 1.0f, 0.0f));
    }

    kAudio *clip = audioPreviewManager->loadAudio(audioPath.string());
    if (clip)
    {
        clip->setLooping(src.loop);
        clip->setVolume(src.volume);
        clip->setPitch(src.pitch);
        clip->setSpatialization(src.spatialize);
        clip->play();
        audioPreviewClip = clip;
        audioPreviewSourceUuid = src.uuid;
        audioPreviewAssetUuid.clear();
    }
}

void Manager::startAudioPreviewByUuid(const kString &audioUuid)
{
    // Toggle: if already previewing this asset, stop.
    if (audioPreviewClip && audioPreviewAssetUuid == audioUuid)
    {
        stopAudioPreview();
        return;
    }

    // Stop any other preview.
    if (audioPreviewClip)
        stopAudioPreview();

    // Resolve to the audio file (Library/ImportedAssets, registry, or
    // stale-UUID metadata recovery).
    fs::path libPath = resolveAudioFileByUuid(audioUuid);
    if (libPath.empty())
    {
        std::cerr << "Audio preview: cannot resolve audio file for UUID " << audioUuid
                  << " (tried Library/ImportedAssets and Assets/)" << std::endl;
        return;
    }
    std::cout << "[Audio Preview] Playing: " << libPath.string() << std::endl;

    if (!audioPreviewManager)
    {
        audioPreviewManager = createAudioManager();
        if (!audioPreviewManager || !audioPreviewManager->init())
        {
            std::cerr << "Audio preview: failed to init audio manager\n";
            audioPreviewManager = nullptr;
            return;
        }
        // Set up a default listener so non-spatialized preview sounds are audible.
        audioPreviewManager->setListenerPosition(kVec3(0.0f));
        audioPreviewManager->setListenerDirection(kVec3(0.0f, 0.0f, -1.0f), kVec3(0.0f, 1.0f, 0.0f));
    }

    kAudio *clip = audioPreviewManager->loadAudio(libPath.string());
    if (clip)
    {
        clip->setLooping(false);
        clip->setVolume(1.0f);
        clip->setPitch(1.0f);
        clip->setSpatialization(false);
        clip->play();
        audioPreviewClip = clip;
        audioPreviewAssetUuid = audioUuid;
        audioPreviewSourceUuid.clear();
    }
}

void Manager::stopAudioPreview()
{
    if (audioPreviewClip && audioPreviewManager)
    {
        audioPreviewClip->stop();
        audioPreviewManager->unloadAudio(audioPreviewClip);
    }
    audioPreviewClip = nullptr;
    audioPreviewSourceUuid.clear();
    audioPreviewAssetUuid.clear();
}

bool Manager::isAudioPreviewPlaying() const
{
    return audioPreviewClip && audioPreviewClip->isPlaying();
}

bool Manager::isAudioAssetPreviewPlaying() const
{
    return audioPreviewClip && audioPreviewClip->isPlaying() && !audioPreviewAssetUuid.empty();
}

// ---------------------------------------------------------------------------
// In-game audio (runtime playback during Game panel Play)
// ---------------------------------------------------------------------------

void Manager::startGameAudio()
{
    // Already running — stop the current clips first to avoid duplicates.
    if (!gameAudioEntries.empty())
        stopGameAudio();

    if (!world)
        return;

    // Create the audio engine once and keep it alive across Play/Stop cycles.
    // Destroying and recreating ma_engine on every stop causes a race with the
    // audio callback thread, leading to access-violation crashes inside
    // ma_engine_uninit.  The engine is destroyed only when the editor exits.
    if (!gameAudioManager)
    {
        gameAudioManager = createAudioManager();
        if (!gameAudioManager || !gameAudioManager->init())
        {
            std::cerr << "[GameAudio] Failed to initialise audio engine.\n";
            delete gameAudioManager;
            gameAudioManager = nullptr;
            return;
        }
    }

    gameAudioHasSpatial = false;
    gameAudioEntries.clear();

    const auto &scenes = world->getScenes();

    // Walk every object in every scene and start audio sources that are
    // both active and marked play-on-awake.
    std::function<void(kObject *)> walk = [&](kObject *node)
    {
        if (!node || !node->getActive())
            return;

        for (const kAudioSource &src : node->getAudioSources())
        {
            if (!src.isActive || !src.playOnAwake || src.audioFile.empty())
                continue;

            // Resolve the audio file path (same logic as startAudioPreview).
            fs::path audioPath = resolveAudioFileByUuid(src.audioFile);

            if (audioPath.empty() || !fs::exists(audioPath))
            {
                std::cerr << "[GameAudio] Cannot resolve audio file for source '"
                          << src.name << "' (UUID " << src.audioFile << ")\n";
                continue;
            }

            kAudio *clip = gameAudioManager->loadAudio(audioPath.string());
            if (!clip)
            {
                std::cerr << "[GameAudio] Failed to load '" << audioPath.string() << "'\n";
                continue;
            }

            // Apply all source properties to the loaded clip.
            clip->setLooping(src.loop);
            clip->setVolume(src.volume);
            clip->setPitch(src.pitch);
            clip->setSpatialization(src.spatialize);
            if (src.spatialize)
            {
                clip->setAttenuationModel(src.attenuationModel);
                clip->setMinDistance(src.minDistance);
                clip->setMaxDistance(src.maxDistance);
                clip->setPosition(node->getGlobalPosition());
            }

            clip->play();

            GameAudioEntry entry;
            entry.clip       = clip;
            entry.object     = node;
            entry.spatialize = src.spatialize;
            gameAudioEntries.push_back(entry);

            if (src.spatialize)
                gameAudioHasSpatial = true;
        }

        for (kObject *child : node->getChildren())
            walk(child);
    };

    for (kScene *s : scenes)
    {
        if (s && s->getRootNode())
            walk(s->getRootNode());
    }

    // If any spatialized audio was started, immediately position the listener
    // so the first audio callback uses the correct position (before the next
    // frame's updateGameAudio call has a chance to run).
    if (gameAudioHasSpatial && gameAudioManager)
    {
        // Prefer an active kAudioListener in the scene, falling back to the
        // default game camera (if set and not the editor camera), then the
        // first non-editor camera.
        kVec3  listenerPos;
        kVec3  listenerForward;
        kVec3  listenerUp;
        bool   haveListener = false;

        // Search for an active kAudioListener first.
        {
            std::function<kObject *(kObject *)> findActiveListener = [&](kObject *node) -> kObject *
            {
                if (!node || !node->getActive())
                    return nullptr;
                for (const kAudioListener &l : node->getAudioListeners())
                    if (l.isActive)
                        return node;
                for (kObject *child : node->getChildren())
                {
                    kObject *found = findActiveListener(child);
                    if (found)
                        return found;
                }
                return nullptr;
            };

            for (kScene *s : scenes)
            {
                if (!s || !s->getRootNode())
                    continue;
                kObject *listenerObj = findActiveListener(s->getRootNode());
                if (listenerObj)
                {
                    listenerPos     = listenerObj->getGlobalPosition();
                    kQuat rot       = listenerObj->getGlobalRotation();
                    listenerForward = glm::rotate(rot, kVec3(0.0f, 0.0f, -1.0f));
                    listenerUp      = glm::rotate(rot, kVec3(0.0f, 1.0f,  0.0f));
                    haveListener    = true;
                    break;
                }
            }
        }

        // Fall back to the default game camera (if not the editor camera).
        if (!haveListener && defaultGameCamera &&
            defaultGameCamera != editorCamera)
        {
            listenerPos     = defaultGameCamera->getGlobalPosition();
            kQuat rot       = defaultGameCamera->getGlobalRotation();
            listenerForward = glm::rotate(rot, kVec3(0.0f, 0.0f, -1.0f));
            listenerUp      = glm::rotate(rot, kVec3(0.0f, 1.0f,  0.0f));
            haveListener    = true;
        }

        // Last resort: first non-editor camera in the world.
        if (!haveListener && world)
        {
            for (kCamera *cam : world->getCameras())
            {
                if (cam != editorCamera)
                {
                    listenerPos     = cam->getGlobalPosition();
                    kQuat rot       = cam->getGlobalRotation();
                    listenerForward = glm::rotate(rot, kVec3(0.0f, 0.0f, -1.0f));
                    listenerUp      = glm::rotate(rot, kVec3(0.0f, 1.0f,  0.0f));
                    haveListener    = true;
                    break;
                }
            }
        }

        if (haveListener)
        {
            gameAudioManager->setListenerPosition(listenerPos);
            gameAudioManager->setListenerDirection(listenerForward, listenerUp);
        }
    }
}

void Manager::stopGameAudio()
{
    if (!gameAudioManager)
        return;

    // Stop and unload every clip that was started by startGameAudio(),
    // but keep the audio engine alive — destroying ma_engine on every
    // stop cycle races with the audio callback thread and crashes.
    for (auto &entry : gameAudioEntries)
    {
        if (entry.clip)
        {
            entry.clip->stop();
            gameAudioManager->unloadAudio(entry.clip);
        }
    }
    gameAudioEntries.clear();
    gameAudioHasSpatial = false;
}

void Manager::pauseGameAudio()
{
    // Pause every clip without resetting its cursor so Play can resume it later.
    for (auto &entry : gameAudioEntries)
    {
        if (entry.clip)
            entry.clip->pause();
    }
}

void Manager::resumeGameAudio()
{
    // Resume clips previously paused by pauseGameAudio(). Clips that finished
    // naturally while paused (or were never paused) are left untouched; resume()
    // is a no-op restart only for sounds that are still valid, mirroring play().
    for (auto &entry : gameAudioEntries)
    {
        if (entry.clip && entry.clip->isPaused())
            entry.clip->resume();
    }
}

void Manager::updateGameAudio(kCamera *gameCamera)
{
    if (!gameAudioManager || gameAudioEntries.empty())
        return;

    if (gameAudioHasSpatial)
    {
        kVec3 listenerPos;
        kVec3 listenerForward;
        kVec3 listenerUp;

        // Find the first active kAudioListener in the world. If one exists,
        // use its world position and orientation as the audio listener.
        // Otherwise fall back to the game camera.
        kObject *activeListener = nullptr;
        if (world)
        {
            for (kScene *s : world->getScenes())
            {
                if (!s || !s->getRootNode())
                    continue;

                std::function<kObject *(kObject *)> findListener = [&](kObject *node) -> kObject *
                {
                    if (!node || !node->getActive())
                        return nullptr;
                    for (const kAudioListener &l : node->getAudioListeners())
                        if (l.isActive)
                            return node;
                    for (kObject *child : node->getChildren())
                    {
                        kObject *found = findListener(child);
                        if (found)
                            return found;
                    }
                    return nullptr;
                };

                activeListener = findListener(s->getRootNode());
                if (activeListener)
                    break;
            }
        }

        if (activeListener)
        {
            listenerPos     = activeListener->getGlobalPosition();
            kQuat rot       = activeListener->getGlobalRotation();
            listenerForward = glm::rotate(rot, kVec3(0.0f, 0.0f, -1.0f));
            listenerUp      = glm::rotate(rot, kVec3(0.0f, 1.0f,  0.0f));
        }
        else if (gameCamera)
        {
            listenerPos     = gameCamera->getGlobalPosition();
            kQuat rot       = gameCamera->getGlobalRotation();
            listenerForward = glm::rotate(rot, kVec3(0.0f, 0.0f, -1.0f));
            listenerUp      = glm::rotate(rot, kVec3(0.0f, 1.0f,  0.0f));
        }
        else
        {
            // No listener and no camera — skip.
            return;
        }

        gameAudioManager->setListenerPosition(listenerPos);
        gameAudioManager->setListenerDirection(listenerForward, listenerUp);
    }

    // Keep each spatialized source positioned at its owning object.
    for (const auto &entry : gameAudioEntries)
    {
        if (entry.spatialize && entry.clip && entry.object)
            entry.clip->setPosition(entry.object->getGlobalPosition());
    }
}

// ---------------------------------------------------------------------------
// Navigation baking
// ---------------------------------------------------------------------------

void Manager::bakeNavMesh(kObject *navObj)
{
    if (!navObj || !scene || !navObj->getHasNavMeshDesc())
        return;

    kNavMeshDesc &desc = navObj->getNavMeshDesc();

    // Area box centred on the nav object (full extents = areaSize).
    navObj->calculateModelMatrix();
    kVec3 center = navObj->getGlobalPosition();
    kVec3 half = desc.areaSize * 0.5f;
    kAABB areaBox(center - half, center + half);

    std::vector<float> verts;
    std::vector<int> tris;

    std::function<void(kObject *)> walk = [&](kObject *node)
    {
        if (!node)
            return;

        // Only static mesh geometry contributes to the walkable surface.
        if (node->getType() == NODE_TYPE_MESH && node->getStatic())
        {
            kMesh *mesh = static_cast<kMesh *>(node);
            mesh->calculateModelMatrix();

            if (!desc.useArea || areaBox.overlaps(mesh->getWorldAABB()))
            {
                kMat4 world = mesh->getModelMatrixWorld();
                std::vector<kVec3> mv = mesh->getVertices();
                std::vector<uint32_t> mi = mesh->getIndices();

                int base = (int)(verts.size() / 3);
                for (const kVec3 &v : mv)
                {
                    kVec3 w = kVec3(world * kVec4(v, 1.0f));
                    verts.push_back(w.x);
                    verts.push_back(w.y);
                    verts.push_back(w.z);
                }
                for (uint32_t idx : mi)
                    tris.push_back(base + (int)idx);
            }
        }
        for (kObject *c : node->getChildren())
            walk(c);
    };
    walk(scene->getRootNode());

    if (verts.empty() || tris.empty())
    {
        std::cerr << "Nav: no static mesh geometry found to bake.\n";
        return;
    }

    // Replace any previous bake for this object.
    clearNavMesh(navObj);

    kNavMesh *nav = new kNavMesh();
    if (nav->bake(verts, tris, desc.config))
    {
        bakedNavMeshes[navObj->getUuid()] = nav;
    }
    else
    {
        std::cerr << "Nav: bake failed.\n";
        delete nav;
    }
}

void Manager::clearNavMesh(kObject *navObj)
{
    if (!navObj)
        return;
    auto it = bakedNavMeshes.find(navObj->getUuid());
    if (it != bakedNavMeshes.end())
    {
        delete it->second;
        bakedNavMeshes.erase(it);
    }
}

void Manager::clearAllNavMeshes()
{
    for (auto &pair : bakedNavMeshes)
        delete pair.second;
    bakedNavMeshes.clear();
}

bool Manager::isNavMeshBaked(kObject *navObj) const
{
    if (!navObj)
        return false;
    auto it = bakedNavMeshes.find(navObj->getUuid());
    return it != bakedNavMeshes.end() && it->second && it->second->isBaked();
}

kNavMesh *Manager::getBakedNavMesh(kObject *navObj) const
{
    if (!navObj)
        return nullptr;
    auto it = bakedNavMeshes.find(navObj->getUuid());
    return it != bakedNavMeshes.end() ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Game export
// ---------------------------------------------------------------------------

bool Manager::exportGame(const ExportSettings &s)
{
    if (!projectOpened)
    {
        std::cerr << "Export: no project open.\n";
        return false;
    }
    if (s.outputDir.empty())
    {
        std::cerr << "Export: no output directory.\n";
        return false;
    }

    fs::path templ = s.templateDir;
    if (templ.empty() || !fs::exists(templ))
    {
        std::cerr << "Export: runtime template folder not found: " << s.templateDir << "\n";
        return false;
    }

    std::error_code ec;

    // 1. Ensure compiled bytecode and an up-to-date scene.world.
    buildScripts();
    saveWorld();

    // 2. Prepare output folder.
    fs::path outDir = s.outputDir;
    fs::create_directories(outDir, ec);

    // 3. Copy the runtime template (player exe + dependency runtime libs).
    for (const auto &entry : fs::directory_iterator(templ, ec))
    {
        fs::copy(entry.path(), outDir / entry.path().filename(),
                 fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
    }

    // 4. Rename the generic runtime executable to the game name.
    const bool win = (s.platform == 0);
    fs::path runExe = outDir / (win ? "Kemena3DRuntime.exe" : "Kemena3DRuntime");
    fs::path gameExe = outDir / (win ? (s.gameName + ".exe") : s.gameName);
    if (fs::exists(runExe))
        fs::rename(runExe, gameExe, ec);

    // 5. Bundle game data into a temp staging folder, then create .kpak.
    fs::path stagingDir = outDir / "_staging";
    fs::create_directories(stagingDir, ec);

    // Copy scene.world
    if (fs::exists(worldPath))
        fs::copy(worldPath, stagingDir / "scene.world", fs::copy_options::overwrite_existing, ec);

    // Copy Library folders into staging
    auto copyLibFolder = [&](const char *sub)
    {
        fs::path src = projectPath / "Library" / sub;
        if (fs::exists(src))
            fs::copy(src, stagingDir / "Library" / sub,
                     fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
    };
    copyLibFolder("ImportedAssets");
    copyLibFolder("Shaders");

    // Scripts: ship compiled bytecode only
    {
        fs::path src = projectPath / "Library" / "Scripts";
        fs::path dst = stagingDir / "Library" / "Scripts";
        if (fs::exists(src))
        {
            fs::create_directories(dst, ec);
            for (const auto &e : fs::directory_iterator(src, ec))
                if (e.path().extension() == ".kbc")
                    fs::copy(e.path(), dst / e.path().filename(),
                             fs::copy_options::overwrite_existing, ec);
        }
    }

    // Write game.config into staging
    {
        nlohmann::json cfg;
        cfg["title"] = s.title.empty() ? s.gameName : s.title;
        cfg["width"] = s.width;
        cfg["height"] = s.height;
        cfg["fullscreen"] = s.fullscreen;
        std::ofstream f(stagingDir / "game.config");
        if (f.is_open())
            f << cfg.dump(4);
    }

    // 6. Create the .kpak package from the staging directory.
    fs::path pakPath = outDir / (s.gameName + ".kpak");
    {
        kPackageWriter writer;
        if (!writer.create(pakPath.string(), stagingDir.string(),
                           kPackageWriter::Compression::Default))
        {
            std::cerr << "Export: failed to create package. Falling back to loose files.\n";
            // Fallback: copy staging contents to data/ folder
            fs::path dataDir = outDir / "data";
            fs::create_directories(dataDir, ec);
            for (const auto &entry : fs::directory_iterator(stagingDir, ec))
            {
                fs::copy(entry.path(), dataDir / entry.path().filename(),
                         fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
            }
        }
    }

    // 7. Clean up staging directory.
    fs::remove_all(stagingDir, ec);

    // 8. Per-OS metadata (best-effort). Windows exe icon via rcedit if present.
    if (win && !s.iconPath.empty() && fs::exists(s.iconPath))
    {
        std::string cmd = "rcedit \"" + gameExe.string() +
                          "\" --set-icon \"" + s.iconPath + "\"";
        int r = std::system(cmd.c_str());
        if (r != 0)
            std::cerr << "Export: rcedit icon step skipped (rcedit not found?).\n";
    }

    std::cout << "Export complete: " << outDir << "\n";
    return true;
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Publish Game (new multi-platform system)
// ---------------------------------------------------------------------------

void Manager::collectWorldAssetPaths(const fs::path &worldPath, std::set<kString> &outAssets)
{
    if (!fs::exists(worldPath)) return;

    std::ifstream f(worldPath);
    if (!f.is_open()) return;

    json worldJson;
    try { worldJson = json::parse(f); }
    catch (...) { return; }
    f.close();

    std::function<void(const json&)> walkJson = [&](const json &node)
    {
        if (node.is_object())
        {
            if (node.contains("reference") && node["reference"].is_string())
            {
                kString ref = node["reference"].get<kString>();
                if (!ref.empty())
                    outAssets.insert("Library/ImportedAssets/" + ref + ".glb");
            }
            if (node.contains("material_uuid") && node["material_uuid"].is_string())
            {
                kString matUuid = node["material_uuid"].get<kString>();
                if (!matUuid.empty())
                {
                    auto it = uuidMap.find(matUuid);
                    if (it != uuidMap.end())
                        outAssets.insert(it->second);
                }
            }
            if (node.contains("script_uuid") && node["script_uuid"].is_string())
            {
                kString sid = node["script_uuid"].get<kString>();
                if (!sid.empty())
                    outAssets.insert("Library/Scripts/" + sid + ".kbc");
            }
            for (const auto &[key, val] : node.items())
                walkJson(val);
        }
        else if (node.is_array())
        {
            for (const auto &item : node)
                walkJson(item);
        }
    };

    walkJson(worldJson);
}

bool Manager::publishGame(int platformIdx)
{
    if (!projectOpened) return false;

    PublishSettings &ps = publishSettings;
    PlatformPublishSettings &plat = ps.platforms[platformIdx];
    if (!plat.enabled) return false;

    if (plat.gameName.empty()) plat.gameName = projectName.empty() ? "Game" : projectName;
    if (plat.title.empty()) plat.title = plat.gameName;
    if (plat.outputDir.empty())
    {
        auto sel = pfd::select_folder("Choose output folder").result();
        if (sel.empty()) return false;
        plat.outputDir = sel;
    }

    if (plat.templateDir.empty()) plat.templateDir = ps.templateDir;
    if (plat.templateDir.empty() || !fs::exists(plat.templateDir))
    {
        auto sel = pfd::select_folder("Choose runtime template folder").result();
        if (sel.empty()) return false;
        plat.templateDir = sel;
    }

    std::error_code ec;
    const bool win = (platformIdx == 0);

    buildScripts();
    saveWorld();

    fs::path outDir = plat.outputDir;
    fs::create_directories(outDir, ec);

    for (const auto &entry : fs::directory_iterator(plat.templateDir, ec))
    {
        fs::copy(entry.path(), outDir / entry.path().filename(),
                 fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
    }

    fs::path runExe = outDir / (win ? "Kemena3DRuntime.exe" : "Kemena3DRuntime");
    fs::path gameExe = outDir / (win ? (plat.gameName + ".exe") : plat.gameName);
    if (fs::exists(runExe))
        fs::rename(runExe, gameExe, ec);

    fs::path stagingDir = outDir / "_staging";
    fs::create_directories(stagingDir, ec);

    std::set<kString> allAssets;

    if (ps.includeWorlds.empty())
    {
        if (!worldPath.empty() && fs::exists(worldPath))
        {
            fs::copy(worldPath, stagingDir / "scene.world", fs::copy_options::overwrite_existing, ec);
            collectWorldAssetPaths(worldPath, allAssets);
        }
    }
    else
    {
        for (const auto &iw : ps.includeWorlds)
        {
            fs::path worldFile = projectPath / "Assets" / iw;
            if (fs::exists(worldFile))
            {
                fs::path destPath = stagingDir / iw;
                fs::create_directories(destPath.parent_path(), ec);
                fs::copy(worldFile, destPath, fs::copy_options::overwrite_existing, ec);
                collectWorldAssetPaths(worldFile, allAssets);
            }
        }
        if (!ps.defaultLevel.empty())
        {
            fs::path defaultWorld = stagingDir / ps.defaultLevel;
            if (fs::exists(defaultWorld))
            {
                fs::copy(defaultWorld, stagingDir / "scene.world", fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    for (const auto &asset : allAssets)
    {
        fs::path src = projectPath / "Assets" / asset;
        fs::path dst = stagingDir / asset;
        if (fs::exists(src))
        {
            fs::create_directories(dst.parent_path(), ec);
            fs::copy(src, dst, fs::copy_options::overwrite_existing, ec);
        }
    }

    auto copyLibIfExists = [&](const char *sub)
    {
        fs::path srcDir = projectPath / "Library" / sub;
        fs::path dstDir = stagingDir / "Library" / sub;
        if (fs::exists(srcDir))
            fs::copy(srcDir, dstDir, fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
    };
    copyLibIfExists("ImportedAssets");
    copyLibIfExists("Shaders");

    {
        fs::path srcDir = projectPath / "Library" / "Scripts";
        fs::path dstDir = stagingDir / "Library" / "Scripts";
        if (fs::exists(srcDir))
        {
            fs::create_directories(dstDir, ec);
            for (const auto &e : fs::directory_iterator(srcDir, ec))
                if (e.path().extension() == ".kbc")
                    fs::copy(e.path(), dstDir / e.path().filename(), fs::copy_options::overwrite_existing, ec);
        }
    }

    {
        nlohmann::json cfg;
        cfg["title"] = plat.title.empty() ? plat.gameName : plat.title;
        cfg["width"] = plat.width;
        cfg["height"] = plat.height;
        cfg["fullscreen"] = plat.fullscreen;
        if (!ps.defaultLevel.empty())
            cfg["default_level"] = ps.defaultLevel;
        std::ofstream cf(stagingDir / "game.config");
        if (cf.is_open()) cf << cfg.dump(4);
    }

    kPackageWriter::Compression comp = kPackageWriter::Compression::Default;
    if (plat.compression == "None") comp = kPackageWriter::Compression::None;
    else if (plat.compression == "Fast") comp = kPackageWriter::Compression::Fast;
    else if (plat.compression == "Best") comp = kPackageWriter::Compression::Best;

    fs::path pakPath = outDir / (plat.gameName + ".kpak");
    {
        kPackageWriter writer;
        if (!writer.create(pakPath.string(), stagingDir.string(), comp))
        {
            std::cerr << "Publish: failed to create package. Falling back to loose files.\n";
            fs::path dataDir = outDir / "data";
            fs::create_directories(dataDir, ec);
            for (const auto &entry : fs::directory_iterator(stagingDir, ec))
                fs::copy(entry.path(), dataDir / entry.path().filename(),
                         fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
        }
    }

    fs::remove_all(stagingDir, ec);

    if (win && !plat.iconPath.empty() && fs::exists(plat.iconPath))
    {
        std::string cmd = "rcedit \"" + gameExe.string() + "\" --set-icon \"" + plat.iconPath + "\"";
        std::system(cmd.c_str());
    }

    std::cout << "Publish complete: " << outDir << "\n";
    return true;
}

void Manager::loadPublishSettings()
{
    if (!projectOpened) return;
    fs::path cfgPath = projectPath / "Config" / "project.json";
    if (!fs::exists(cfgPath)) return;
    std::ifstream f(cfgPath);
    if (!f.is_open()) return;
    json cfg;
    try { cfg = json::parse(f); } catch (...) { return; }
    f.close();

    PublishSettings &ps = publishSettings;
    if (cfg.contains("publish_settings") && cfg["publish_settings"].is_object())
    {
        const json &pjs = cfg["publish_settings"];
        const char *keys[] = {"windows", "macos", "linux"};
        for (int i = 0; i < 3; ++i)
        {
            if (!pjs.contains(keys[i])) continue;
            const json &pj = pjs[keys[i]];
            ps.platforms[i].enabled     = pj.value("enabled", true);
            ps.platforms[i].gameName    = pj.value("game_name", "Game");
            ps.platforms[i].title       = pj.value("title", "My Game");
            ps.platforms[i].width       = pj.value("width", 1280);
            ps.platforms[i].height      = pj.value("height", 720);
            ps.platforms[i].fullscreen  = pj.value("fullscreen", false);
            ps.platforms[i].outputDir   = pj.value("output_dir", "");
            ps.platforms[i].templateDir = pj.value("template_dir", "");
            ps.platforms[i].iconPath    = pj.value("icon_path", "");
            ps.platforms[i].compression = pj.value("compression", "Default");
        }
    }
    if (cfg.contains("publish_worlds") && cfg["publish_worlds"].is_array())
    {
        ps.includeWorlds.clear();
        for (const auto &w : cfg["publish_worlds"])
            if (w.is_string())
                ps.includeWorlds.push_back(w.get<std::string>());
    }
    ps.defaultLevel = cfg.value("default_level", "");
    ps.templateDir = cfg.value("template_dir", "");
}

void Manager::savePublishSettings()
{
    if (!projectOpened) return;
    fs::path cfgPath = projectPath / "Config" / "project.json";
    json cfg;
    if (fs::exists(cfgPath))
    {
        std::ifstream f(cfgPath);
        if (f.is_open()) { try { cfg = json::parse(f); } catch (...) { cfg = json::object(); } f.close(); }
    }

    PublishSettings &ps = publishSettings;
    json pjs = json::object();
    const char *keys[] = {"windows", "macos", "linux"};
    for (int i = 0; i < 3; ++i)
    {
        json pj;
        pj["enabled"]      = ps.platforms[i].enabled;
        pj["game_name"]    = ps.platforms[i].gameName;
        pj["title"]        = ps.platforms[i].title;
        pj["width"]        = ps.platforms[i].width;
        pj["height"]       = ps.platforms[i].height;
        pj["fullscreen"]   = ps.platforms[i].fullscreen;
        pj["output_dir"]   = ps.platforms[i].outputDir;
        pj["template_dir"] = ps.platforms[i].templateDir;
        pj["icon_path"]    = ps.platforms[i].iconPath;
        pj["compression"]  = ps.platforms[i].compression;
        pjs[keys[i]] = pj;
    }
    cfg["publish_settings"] = pjs;

    json worldsJson = json::array();
    for (const auto &w : ps.includeWorlds)
        worldsJson.push_back(w);
    cfg["publish_worlds"] = worldsJson;
    cfg["default_level"] = ps.defaultLevel;
    cfg["template_dir"]  = ps.templateDir;

    std::ofstream f(cfgPath);
    if (f.is_open()) f << cfg.dump(4);
}

void Manager::loadInputSettings()
{
    if (!projectOpened) return;
    fs::path cfgPath = projectPath / "Config" / "project.json";
    if (!fs::exists(cfgPath)) return;
    std::ifstream f(cfgPath);
    if (!f.is_open()) return;
    json cfg;
    try { cfg = json::parse(f); } catch (...) { return; }
    f.close();

    inputSettings.actions.clear();
    if (cfg.contains("input_settings") && cfg["input_settings"].is_object())
    {
        const json &ijs = cfg["input_settings"];
        if (ijs.contains("actions") && ijs["actions"].is_array())
        {
            for (const auto &a : ijs["actions"])
            {
                if (!a.is_object()) continue;
                InputActionBinding b;
                b.name = a.value("name", "");
                b.device = a.value("device", (int)INPUT_BINDING_KEYBOARD);
                b.binding = a.value("binding", 0);
                if (!b.name.empty())
                    inputSettings.actions.push_back(b);
            }
        }
    }
}

void Manager::saveInputSettings()
{
    if (!projectOpened) return;
    fs::path cfgPath = projectPath / "Config" / "project.json";
    json cfg;
    if (fs::exists(cfgPath))
    {
        std::ifstream f(cfgPath);
        if (f.is_open()) { try { cfg = json::parse(f); } catch (...) { cfg = json::object(); } f.close(); }
    }

    json actionsJson = json::array();
    for (const auto &a : inputSettings.actions)
    {
        json aj;
        aj["name"] = a.name;
        aj["device"] = a.device;
        aj["binding"] = a.binding;
        actionsJson.push_back(aj);
    }
    json ijs = json::object();
    ijs["actions"] = actionsJson;
    cfg["input_settings"] = ijs;

    std::ofstream f(cfgPath);
    if (f.is_open()) f << cfg.dump(4);
}

void Manager::loadTagSettings()
{
    tagSettings.tags.clear();
    // Default: a minimal Unity-style set. "(Untagged)" is the built-in empty
    // tag handled by the inspector directly, so it is not listed here.
    tagSettings.tags = { "Player", "Enemy", "MainCamera", "Ground", "Respawn" };
    if (!projectOpened) return;
    fs::path cfgPath = projectPath / "Config" / "project.json";
    if (!fs::exists(cfgPath)) return;
    std::ifstream f(cfgPath);
    if (!f.is_open()) return;
    json cfg;
    try { cfg = json::parse(f); } catch (...) { return; }
    f.close();

    if (cfg.contains("tag_settings") && cfg["tag_settings"].is_object())
    {
        const json &ts = cfg["tag_settings"];
        if (ts.contains("tags") && ts["tags"].is_array())
        {
            tagSettings.tags.clear();
            for (const auto &t : ts["tags"])
                if (t.is_string())
                    tagSettings.tags.push_back(t.get<std::string>());
        }
    }
}

void Manager::saveTagSettings()
{
    if (!projectOpened) return;
    fs::path cfgPath = projectPath / "Config" / "project.json";
    json cfg;
    if (fs::exists(cfgPath))
    {
        std::ifstream f(cfgPath);
        if (f.is_open()) { try { cfg = json::parse(f); } catch (...) { cfg = json::object(); } f.close(); }
    }

    json tagsJson = json::array();
    for (const auto &t : tagSettings.tags)
        tagsJson.push_back(t);
    json ts = json::object();
    ts["tags"] = tagsJson;
    cfg["tag_settings"] = ts;

    std::ofstream f(cfgPath);
    if (f.is_open()) f << cfg.dump(4);
}

void Manager::loadLayerSettings()
{
    layerSettings.layers.clear();
    // The "Default" layer is always present (index 0).
    layerSettings.layers.push_back("Default");
    if (!projectOpened) return;
    fs::path cfgPath = projectPath / "Config" / "project.json";
    if (!fs::exists(cfgPath)) return;
    std::ifstream f(cfgPath);
    if (!f.is_open()) return;
    json cfg;
    try { cfg = json::parse(f); } catch (...) { return; }
    f.close();

    if (cfg.contains("layer_settings") && cfg["layer_settings"].is_object())
    {
        const json &ls = cfg["layer_settings"];
        if (ls.contains("layers") && ls["layers"].is_array())
        {
            layerSettings.layers.clear();
            for (const auto &l : ls["layers"])
                if (l.is_string())
                    layerSettings.layers.push_back(l.get<std::string>());
        }
    }
    // Guarantee "Default" is first.
    bool hasDefault = false;
    for (const auto &l : layerSettings.layers)
        if (l == "Default") { hasDefault = true; break; }
    if (!hasDefault)
        layerSettings.layers.insert(layerSettings.layers.begin(), "Default");
}

void Manager::saveLayerSettings()
{
    if (!projectOpened) return;
    fs::path cfgPath = projectPath / "Config" / "project.json";
    json cfg;
    if (fs::exists(cfgPath))
    {
        std::ifstream f(cfgPath);
        if (f.is_open()) { try { cfg = json::parse(f); } catch (...) { cfg = json::object(); } f.close(); }
    }

    json layersJson = json::array();
    for (const auto &l : layerSettings.layers)
        layersJson.push_back(l);
    json ls = json::object();
    ls["layers"] = layersJson;
    cfg["layer_settings"] = ls;

    std::ofstream f(cfgPath);
    if (f.is_open()) f << cfg.dump(4);
}

bool Manager::addTag(const std::string &name)
{
    std::string trimmed = name;
    // trim whitespace
    size_t b = trimmed.find_first_not_of(" \t\r\n");
    size_t e = trimmed.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    trimmed = trimmed.substr(b, e - b + 1);
    if (trimmed.empty()) return false;

    for (const auto &t : tagSettings.tags)
        if (t == trimmed) return false;

    tagSettings.tags.push_back(trimmed);
    saveTagSettings();
    return true;
}

void Manager::removeTag(const std::string &name)
{
    for (size_t i = 0; i < tagSettings.tags.size(); ++i)
    {
        if (tagSettings.tags[i] == name)
        {
            tagSettings.tags.erase(tagSettings.tags.begin() + i);
            break;
        }
    }
    saveTagSettings();
}

bool Manager::addLayer(const std::string &name)
{
    std::string trimmed = name;
    size_t b = trimmed.find_first_not_of(" \t\r\n");
    size_t e = trimmed.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) return false;
    trimmed = trimmed.substr(b, e - b + 1);
    if (trimmed.empty()) return false;

    for (const auto &l : layerSettings.layers)
        if (l == trimmed) return false;

    if ((int)layerSettings.layers.size() >= kMaxPhysicsLayers)
        return false;

    layerSettings.layers.push_back(trimmed);
    saveLayerSettings();
    return true;
}

void Manager::removeLayer(const std::string &name)
{
    if (name == "Default") return; // never remove the built-in layer
    for (size_t i = 0; i < layerSettings.layers.size(); ++i)
    {
        if (layerSettings.layers[i] == name)
        {
            layerSettings.layers.erase(layerSettings.layers.begin() + i);
            break;
        }
    }
    saveLayerSettings();
}

// ---------------------------------------------------------------------------
// Create New Material asset file
// ---------------------------------------------------------------------------

void Manager::selectProjectAssetByPath(const fs::path &filePath)
{
    if (!panelProject)
        return;
    std::error_code ec;
    kString rel = fs::relative(filePath, projectPath / "Assets", ec).generic_string();
    auto it = uuidMap.find(rel);
    if (it != uuidMap.end())
    {
        panelProject->pendingSelectUuid = it->second;
        panelProject->triggerRefresh();
    }
}

void Manager::createNewMaterial()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();

    kString baseName = "New Material";
    fs::path filePath = dir / (baseName + ".mat");
    int counter = 1;
    while (fs::exists(filePath))
    {
        filePath = dir / (baseName + " " + std::to_string(counter) + ".mat");
        counter++;
    }

    kString matUuid = generateUuid();

    nlohmann::json mat;
    mat["uuid"] = matUuid;
    mat["shader"] = "Phong";
    mat["diffuse"] = {1.0f, 1.0f, 1.0f};
    mat["ambient"] = {1.0f, 1.0f, 1.0f};
    mat["specular"] = {1.0f, 1.0f, 1.0f};
    mat["shininess"] = 32.0f;
    mat["metallic"] = 0.0f;
    mat["roughness"] = 0.5f;
    mat["glossiness"] = 1.0f;
    mat["uv_tiling"] = {1.0f, 1.0f};
    mat["transparent"] = 0;
    mat["single_sided"] = true;
    mat["cull_back"] = true;
    mat["texture_albedo"] = "";
    mat["texture_normal"] = "";
    mat["texture_specular"] = "";
    mat["texture_glossiness"] = "";
    mat["texture_metallic_roughness"] = "";
    mat["texture_ao"] = "";
    mat["texture_emissive"] = "";

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create material file: " << filePath << "\n";
        return;
    }
    f << mat.dump(4);
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewRawShader()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Raw Shader";
    fs::path filePath = dir / (baseName + ".glsl");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".glsl");

    // A working unlit, textured template. The `// @var` lines tell the material
    // inspector which uniforms to expose (and which control to draw).
    static const char *kTemplate =
        "// --- VERTEX ---\n"
        "#version 330 core\n"
        "layout(location = 0) in vec3 vertexPosition;\n"
        "layout(location = 2) in vec2 texCoord;\n"
        "uniform mat4 modelMatrix;\n"
        "uniform mat4 viewMatrix;\n"
        "uniform mat4 projectionMatrix;\n"
        "out vec2 texCoordFrag;\n"
        "void main()\n"
        "{\n"
        "    texCoordFrag = texCoord;\n"
        "    gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(vertexPosition, 1.0);\n"
        "}\n"
        "\n"
        "// --- FRAGMENT ---\n"
        "#version 330 core\n"
        "in vec2 texCoordFrag;\n"
        "out vec4 fragColor;\n"
        "\n"
        "// Material parameters exposed to the inspector:\n"
        "// @var vec3      tint      Tint\n"
        "// @var sampler2D albedoMap Albedo\n"
        "uniform vec3      tint;\n"
        "uniform sampler2D albedoMap;\n"
        "uniform bool      has_albedoMap;\n"
        "\n"
        "void main()\n"
        "{\n"
        "    vec4 base = has_albedoMap ? texture(albedoMap, texCoordFrag) : vec4(1.0);\n"
        "    fragColor = vec4(tint, 1.0) * base;\n"
        "}\n";

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create shader file: " << filePath << "\n";
        return;
    }
    f << kTemplate;
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewFolder()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Folder";
    fs::path folderPath = dir / baseName;
    int counter = 1;
    while (fs::exists(folderPath))
        folderPath = dir / (baseName + " " + std::to_string(counter++));

    std::error_code ec;
    fs::create_directory(folderPath, ec);
    if (ec)
    {
        std::cerr << "Failed to create folder: " << folderPath << " (" << ec.message() << ")\n";
        return;
    }
    checkAssetChange();

    // Select the new folder. Folders aren't tracked in fileMap/uuidMap; their
    // view node UUID is "Folder_<name>", which is what pendingSelectUuid matches.
    if (panelProject)
    {
        panelProject->pendingSelectUuid = "Folder_" + folderPath.filename().string();
        panelProject->triggerRefresh();
    }
}

void Manager::createNewShader()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Shader";
    fs::path filePath = dir / (baseName + ".shader");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".shader");

    // Seed with a default PBR output node so the graph opens ready to edit.
    kShaderGraph graph;
    graph.uuid = generateUuid();
    graph.name = filePath.stem().string();
    graph.nodes.push_back(graph.makeNode(kShaderNodeType::OutputPBR, 500.0f, 200.0f));

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create shader file: " << filePath << "\n";
        return;
    }
    f << graph.toJson().dump(4);
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewScript()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Script";
    fs::path filePath = dir / (baseName + ".as");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".as");

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create script file: " << filePath << "\n";
        return;
    }
    f << "// AngelScript — attach to an object's Scripts component in the Inspector.\n"
         "// Host API: getSelf(), getDeltaTime(), print(string), kVec3, kObject.\n\n"
         "void Start()\n"
         "{\n"
         "}\n\n"
         "void Update()\n"
         "{\n"
         "}\n";
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewLogicGraph()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Logic Graph";
    fs::path filePath = dir / (baseName + ".logic");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".logic");

    // Seed with an On Update event so the canvas is not empty.
    kScriptGraph graph;
    graph.uuid = generateUuid();
    graph.name = filePath.stem().string();
    graph.nodes.push_back(graph.makeNode(kScriptNodeType::EventUpdate, 120.0f, 120.0f));

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create logic graph file: " << filePath << "\n";
        return;
    }
    f << graph.toJson().dump(2);
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewAnimator()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Animator";
    fs::path filePath = dir / (baseName + ".animator");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".animator");

    // Write a minimal .animator with a default entry state.
    nlohmann::json j;
    j["uuid"] = generateUuid();
    j["name"] = filePath.stem().string();
    j["nextNodeId"] = 2;
    j["nextLinkId"] = 1;
    j["clips"] = nlohmann::json::array();
    j["variables"] = nlohmann::json::array();

    nlohmann::json entryState;
    entryState["id"]        = 1;
    entryState["name"]      = "Default State";
    entryState["animationUuid"] = "";
    entryState["speed"]     = 1.0f;
    entryState["loop"]      = true;
    entryState["isDefault"] = true;
    entryState["posX"]      = 300.0f;
    entryState["posY"]      = 200.0f;
    j["states"] = nlohmann::json::array({entryState});
    j["transitions"] = nlohmann::json::array();

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create animator file: " << filePath << "\n";
        return;
    }
    f << j.dump(4);
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewAnimation()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Cinematic";
    fs::path filePath = dir / (baseName + ".cinematic");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".cinematic");

    // Write a default .cinematic file (scene animation).
    nlohmann::json j;
    j["uuid"]       = generateUuid();
    j["name"]       = filePath.stem().string();
    j["type"]       = "scene";
    j["duration"]   = 5.0f;
    j["fps"]        = 30.0f;
    j["tracks"]     = nlohmann::json::array();
    j["events"]     = nlohmann::json::array();

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create cinematic file: " << filePath << "\n";
        return;
    }
    f << j.dump(4);
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewParticle()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Particle System";
    fs::path filePath = dir / (baseName + ".particle");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".particle");

    nlohmann::json j;
    j["uuid"]     = generateUuid();
    j["name"]     = filePath.stem().string();
    j["duration"] = 5.0f;
    j["looping"]  = true;
    j["emitters"] = nlohmann::json::array();

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create particle file: " << filePath << "\n";
        return;
    }
    f << j.dump(4);
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewGui()
{
    if (!projectOpened)
        return;

    fs::path dir = getCurrentDirPath();
    kString baseName = "New Ingame UI";
    fs::path filePath = dir / (baseName + ".ui");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".ui");

    // Minimal .ui document: one full-canvas panel with a centred title label.
    // Matches the schema written by PanelGui (see GuiLayout::toJson).
    const int canvasW = 1920;
    const int canvasH = 1080;

    nlohmann::json panel;
    panel["id"] = 1;
    panel["parentId"] = -1;
    panel["type"] = "Panel";
    panel["name"] = "Root Panel";
    panel["x"] = 0.0f; panel["y"] = 0.0f;
    panel["width"] = (float)canvasW; panel["height"] = (float)canvasH;
    panel["anchor"] = 0;
    panel["rotation"] = 0.0f; panel["scale"] = 1.0f;
    panel["visible"] = true; panel["interactable"] = true;
    panel["color"] = nlohmann::json::array({ 0.0f, 0.0f, 0.0f, 0.0f });
    panel["textColor"] = nlohmann::json::array({ 1.0f, 1.0f, 1.0f, 1.0f });
    panel["borderColor"] = nlohmann::json::array({ 0.35f, 0.38f, 0.42f, 1.0f });
    panel["borderWidth"] = 0.0f; panel["cornerRadius"] = 0.0f;
    panel["fillBackground"] = true; panel["showBorder"] = false;
    panel["sliceLeft"] = 8.0f; panel["sliceRight"] = 8.0f;
    panel["sliceTop"] = 8.0f; panel["sliceBottom"] = 8.0f;
    panel["showCorners"] = true;
    panel["text"] = "Text"; panel["fontSize"] = 16.0f;
    panel["textAlign"] = 1; panel["wordWrap"] = false;
    panel["textureUuid"] = ""; panel["imageMode"] = 0; panel["fillAmount"] = 1.0f;
    panel["scrollDirection"] = 0; panel["showScrollbar"] = true; panel["scrollPosition"] = 0.0f;
    panel["minValue"] = 0.0f; panel["maxValue"] = 1.0f; panel["value"] = 0.5f;
    panel["checked"] = true; panel["vertical"] = false;

    nlohmann::json title;
    title["id"] = 2;
    title["parentId"] = 1;
    title["type"] = "Text";
    title["name"] = "Title";
    title["x"] = 0.0f; title["y"] = 40.0f;
    title["width"] = (float)canvasW; title["height"] = 64.0f;
    title["anchor"] = 1; // Top
    title["rotation"] = 0.0f; title["scale"] = 1.0f;
    title["visible"] = true; title["interactable"] = true;
    title["color"] = nlohmann::json::array({ 0.2f, 0.22f, 0.26f, 1.0f });
    title["textColor"] = nlohmann::json::array({ 1.0f, 1.0f, 1.0f, 1.0f });
    title["borderColor"] = nlohmann::json::array({ 0.35f, 0.38f, 0.42f, 1.0f });
    title["borderWidth"] = 0.0f; title["cornerRadius"] = 0.0f;
    title["fillBackground"] = false; title["showBorder"] = false;
    title["sliceLeft"] = 8.0f; title["sliceRight"] = 8.0f;
    title["sliceTop"] = 8.0f; title["sliceBottom"] = 8.0f;
    title["showCorners"] = true;
    title["text"] = "New Ingame UI"; title["fontSize"] = 40.0f;
    title["textAlign"] = 1; title["wordWrap"] = false;
    title["textureUuid"] = ""; title["imageMode"] = 0; title["fillAmount"] = 1.0f;
    title["scrollDirection"] = 0; title["showScrollbar"] = true; title["scrollPosition"] = 0.0f;
    title["minValue"] = 0.0f; title["maxValue"] = 1.0f; title["value"] = 0.5f;
    title["checked"] = true; title["vertical"] = false;

    nlohmann::json j;
    j["uuid"] = generateUuid();
    j["name"] = filePath.stem().string();
    j["version"] = 1;
    j["canvasWidth"] = canvasW;
    j["canvasHeight"] = canvasH;
    j["canvasColor"] = nlohmann::json::array({ 0.10f, 0.11f, 0.13f, 1.0f });
    j["showCanvasGrid"] = true;
    j["nextId"] = 3;
    j["widgets"] = nlohmann::json::array({ panel, title });

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create UI file: " << filePath << "\n";
        return;
    }
    f << j.dump(4);
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

void Manager::createNewAnimationFromMesh(const kString &meshUuid, const fs::path &meshPath)
{
    if (!projectOpened)
        return;

    // Create the .animation file in the same folder as the mesh
    fs::path dir = meshPath.parent_path();
    kString baseName = meshPath.stem().string() + " Animation";
    fs::path filePath = dir / (baseName + ".animation");
    int counter = 1;
    while (fs::exists(filePath))
        filePath = dir / (baseName + " " + std::to_string(counter++) + ".animation");

    // End the clip at the source file's longest animation instead of assuming
    // 30 frames. Fallback remains 30 when the source has no animation.
    int endFrame = getMaxAnimationFrames(meshPath);

    // Write a mesh-linked .animation file.
    nlohmann::json j;
    j["uuid"]       = generateUuid();
    j["name"]       = filePath.stem().string();
    j["type"]       = "mesh";
    j["meshUuid"]   = meshUuid;
    j["startFrame"] = 0;
    j["endFrame"]   = endFrame;
    // Root-motion extraction options (off by default). When enabled, scripts
    // can read the corresponding root-motion delta from the animator and the
    // channel is baked out of the animation preview pose.
    j["rootMotionRotation"]  = false;
    j["rootMotionPositionY"] = false;
    j["rootMotionPositionXZ"] = false;

    std::ofstream f(filePath);
    if (!f.is_open())
    {
        std::cerr << "Failed to create animation file: " << filePath << "\n";
        return;
    }
    f << j.dump(4);
    f.close();

    checkAssetChange();
    selectProjectAssetByPath(filePath);
}

// Carry an asset's UUID to its new path in assets.json after a move/rename.
// Without this, checkAssetChange() (which keys the registry by path) treats the
// file as brand new, assigns a fresh UUID, and orphans the old one — breaking
// every reference to it (materials, generated logic-graph scripts, etc.).
// Handles both a single file (exact match) and a renamed/moved folder (every
// entry whose path is under it gets its prefix rewritten).
static void remapAssetRegistryPath(const fs::path &projectPath,
                                   const fs::path &srcPath, const fs::path &newPath)
{
    std::error_code ec;
    fs::path assetsPath = projectPath / "Assets";
    fs::path assetsJsonFile = projectPath / "Library" / "assets.json";
    std::string oldRel = fs::relative(srcPath, assetsPath, ec).generic_string();
    std::string newRel = fs::relative(newPath, assetsPath, ec).generic_string();
    if (oldRel.empty() || newRel.empty() || !fs::exists(assetsJsonFile))
        return;

    try
    {
        nlohmann::json j;
        {
            std::ifstream ifs(assetsJsonFile);
            ifs >> j;
        }
        if (!j.contains("files") || !j["files"].is_array())
            return;

        const std::string oldPrefix = oldRel + "/";
        for (auto &entry : j["files"])
        {
            std::string name = entry.value("name", std::string());
            if (name == oldRel)
                entry["name"] = newRel; // the file/folder itself
            else if (name.rfind(oldPrefix, 0) == 0)
                entry["name"] = newRel + name.substr(oldRel.size()); // a child of a renamed folder
        }
        std::ofstream ofs(assetsJsonFile);
        ofs << j.dump(4);
    }
    catch (const std::exception &e)
    {
        std::cerr << "remapAssetRegistryPath: " << e.what() << "\n";
    }
}

bool Manager::moveAsset(const fs::path &srcPath, const fs::path &destDir)
{
    if (!projectOpened || srcPath.empty() || destDir.empty())
        return false;

    std::error_code ec;
    if (!fs::exists(srcPath, ec) || ec)
    {
        std::cerr << "Move failed — source does not exist: " << srcPath << "\n";
        return false;
    }
    if (!fs::is_directory(destDir, ec) || ec)
    {
        std::cerr << "Move failed — destination is not a folder: " << destDir << "\n";
        return false;
    }

    fs::path newPath = destDir / srcPath.filename();
    if (newPath == srcPath)
        return true; // already in that folder

    if (fs::exists(newPath, ec))
    {
        std::cerr << "Move failed — '" << newPath.filename().string()
                  << "' already exists in the target folder.\n";
        return false;
    }

    fs::rename(srcPath, newPath, ec);
    if (ec)
    {
        std::cerr << "Move failed: " << ec.message() << "\n";
        return false;
    }

    // Carry the asset's UUID to its new path so references survive the move.
    remapAssetRegistryPath(projectPath, srcPath, newPath);

    // Keep the Script Editor's open-file path in sync if this file moved.
    if (panelLogicGraph)
        panelLogicGraph->notifyAssetMoved(srcPath.string(), newPath.string());

    // Rebuild Manager::fileMap from the patched assets.json. The moved file now
    // matches an existing path+checksum, so no re-import and the UUID is kept.
    checkAssetChange();
    return true;
}

bool Manager::renameAsset(const fs::path &oldPath, const kString &newName)
{
    if (!projectOpened || newName.empty())
        return false;

    fs::path newPath = oldPath.parent_path() / newName;
    if (newPath == oldPath)
        return true;

    if (fs::exists(newPath))
    {
        std::cerr << "Rename failed — a file named '" << newName << "' already exists.\n";
        return false;
    }

    std::error_code ec;
    fs::rename(oldPath, newPath, ec);
    if (ec)
    {
        std::cerr << "Rename failed: " << ec.message() << "\n";
        return false;
    }

    // Terrain rename: also rename the associated _data folder
    if (oldPath.extension() == ".terrain" && panelTerrain)
    {
        panelTerrain->onTerrainFileRenamed(oldPath.string(), newPath.string());
    }

    // Preserve the asset's UUID across the rename so its generated script (for a
    // .logic graph), material references, etc. keep resolving. Without this,
    // checkAssetChange() would assign a fresh UUID and the links break.
    remapAssetRegistryPath(projectPath, oldPath, newPath);

    // If the renamed file is open in the Script Editor, update its tracked path
    // so a subsequent Save writes to the new file instead of the old name.
    if (panelLogicGraph)
        panelLogicGraph->notifyAssetMoved(oldPath.string(), newPath.string());

    checkAssetChange();
    return true;
}

bool Manager::duplicateAsset(const fs::path &srcPath)
{
    if (!projectOpened || srcPath.empty() || !fs::exists(srcPath))
        return false;

    const bool isDir = fs::is_directory(srcPath);
    fs::path dir = srcPath.parent_path();
    std::string stem = srcPath.stem().string();
    std::string ext = srcPath.extension().string();

    // Find a free "<stem> copy[ N][.ext]" name.
    auto candidate = [&](int n) -> fs::path
    {
        std::string base = stem + " copy" + (n > 1 ? " " + std::to_string(n) : "");
        return isDir ? (dir / base) : (dir / (base + ext));
    };
    int n = 1;
    fs::path dst = candidate(n);
    while (fs::exists(dst))
        dst = candidate(++n);

    std::error_code ec;
    if (isDir)
        fs::copy(srcPath, dst, fs::copy_options::recursive, ec);
    else
        fs::copy_file(srcPath, dst, ec);
    if (ec)
    {
        std::cerr << "Duplicate failed: " << ec.message() << "\n";
        return false;
    }

    // Regenerate the embedded UUID of JSON assets so the copy is independent of
    // the original (notably .logic, whose UUID names its generated script).
    auto regenEmbeddedUuid = [](const fs::path &p)
    {
        std::string e = p.extension().string();
        if (e != ".mat" && e != ".logic" && e != ".shader")
            return;
        try
        {
            nlohmann::json j;
            {
                std::ifstream in(p);
                if (!in.is_open())
                    return;
                in >> j;
            }
            if (j.contains("uuid"))
                j["uuid"] = generateUuid();
            std::ofstream out(p);
            if (out.is_open())
                out << j.dump(4);
        }
        catch (...)
        {
        }
    };
    if (isDir)
    {
        for (auto it = fs::recursive_directory_iterator(dst, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
            if (!ec && it->is_regular_file())
                regenEmbeddedUuid(it->path());
    }
    else
    {
        regenEmbeddedUuid(dst);
    }

    checkAssetChange();
    selectProjectAssetByPath(dst);
    return true;
}

// ---------------------------------------------------------------------------
// Save / Load world
// ---------------------------------------------------------------------------

void Manager::saveProjectConfig()
{
    if (!projectOpened || projectPath.empty())
        return;

    fs::path cfgDir = projectPath / "Config";
    std::error_code ec;
    fs::create_directories(cfgDir, ec);
    fs::path cfgFile = cfgDir / "project.json";

    try
    {
        json j;
        if (fs::exists(cfgFile))
        {
            std::ifstream in(cfgFile);
            try
            {
                in >> j;
            }
            catch (...)
            {
            }
        }
        // Persist the project name so it survives across sessions.
        if (!projectName.empty())
            j["project_name"] = projectName;
        if (!worldPath.empty() && fs::exists(worldPath))
        {
            std::error_code rel_ec;
            fs::path rel = fs::relative(worldPath, projectPath, rel_ec);
            j["last_world"] = rel_ec ? worldPath.string()
                                     : rel.generic_string();
        }
        else
        {
            j["last_world"] = "";
        }
        std::ofstream out(cfgFile);
        if (out.is_open())
            out << j.dump(4);
    }
    catch (const std::exception &e)
    {
        std::cerr << "saveProjectConfig: " << e.what() << "\n";
    }
}

fs::path Manager::loadLastWorldPath() const
{
    if (projectPath.empty())
        return {};

    // Preferred: the explicit "last_world" record written by saveWorld.
    fs::path cfgFile = projectPath / "Config" / "project.json";
    if (fs::exists(cfgFile))
    {
        try
        {
            std::ifstream in(cfgFile);
            json j;
            in >> j;
            std::string rel = j.value("last_world", std::string());
            if (!rel.empty())
            {
                fs::path abs = projectPath / rel;
                if (fs::exists(abs))
                    return abs;
            }
        }
        catch (...)
        {
        }
    }

    // Fallback A: any .world file under Assets/. Pick the most-recently-
    // modified one so a project that has multiple .world files surfaces the
    // one the user most likely worked on last.
    fs::path assetsDir = projectPath / "Assets";
    if (fs::exists(assetsDir))
    {
        std::error_code ec;
        fs::path best;
        fs::file_time_type bestTime{};
        for (auto it = fs::recursive_directory_iterator(assetsDir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
                break;
            if (!it->is_regular_file())
                continue;
            if (it->path().extension() != ".world")
                continue;
            auto mt = fs::last_write_time(it->path(), ec);
            if (best.empty() || mt > bestTime)
            {
                best = it->path();
                bestTime = mt;
            }
        }
        if (!best.empty())
            return best;
    }

    // Fallback B: pre-Assets-rule projects kept the world at the project root.
    fs::path legacy = projectPath / "scene.world";
    if (fs::exists(legacy))
        return legacy;

    return {};
}

namespace
{
    // True when `child` lives inside `parent` (or equals a file directly in it).
    // Uses weakly_canonical so the comparison survives relative segments and
    // mixed separators that pfd may return on Windows.
    bool isPathUnder(const fs::path &child, const fs::path &parent)
    {
        std::error_code ec;
        fs::path c = fs::weakly_canonical(child, ec);
        fs::path p = fs::weakly_canonical(parent, ec);
        if (ec)
            return false;
        fs::path rel = c.lexically_relative(p);
        if (rel.empty())
            return false;
        auto it = rel.begin();
        return it != rel.end() && *it != "..";
    }
}

kString Manager::promptWorldSavePath()
{
    if (!projectOpened)
        return "";

    fs::path assetsDir = projectPath / "Assets";
    std::error_code ec;
    fs::create_directories(assetsDir, ec);

    showingMessageBox = true;
    std::string chosen = pfd::save_file(
                             "Save World", assetsDir.string(),
                             {"World Files", "*.world", "All Files", "*"})
                             .result();
    showingMessageBox = false;

    if (chosen.empty())
        return "";

    fs::path p = chosen;
    if (p.extension() != ".world")
        p += ".world";

    if (!isPathUnder(p, assetsDir))
    {
        showingMessageBox = true;
        pfd::message(
            "Invalid Save Location",
            "Worlds can only be saved inside the project's Assets folder.\n\n"
            "Selected:  " +
                p.string() + "\n"
                             "Required:  " +
                assetsDir.string(),
            pfd::choice::ok, pfd::icon::warning)
            .result();
        showingMessageBox = false;
        return "";
    }

    return p.string();
}

void Manager::applyDefaultSkybox(kScene *target)
{
    if (!target)
        return;
    kAssetManager *am = getAssetManager();
    if (!am)
        return;

    kShader *skyShader = am->loadGlslFromResource("SHADER_SKYBOX");
    kMaterial *skyMat = am->createMaterial(skyShader);
    kTextureCube *skyTex = am->loadTextureCubeFromResource(
        "TEXTURE_SKYBOX_RIGHT", "TEXTURE_SKYBOX_LEFT",
        "TEXTURE_SKYBOX_TOP", "TEXTURE_SKYBOX_BOTTOM",
        "TEXTURE_SKYBOX_FRONT", "TEXTURE_SKYBOX_BACK",
        "cubeMap");
    skyMat->addTexture(skyTex);
    skyMat->setSingleSided(false);

    kMesh *skyMesh = kMeshGenerator::generateCube();
    skyMesh->setMaterial(skyMat);
    target->setSkybox(skyMat, skyMesh);
}

void Manager::saveWorld()
{
    if (!projectOpened || !world)
        return;

    fs::path assetsDir = projectPath / "Assets";

    // Untitled, or pointing somewhere it shouldn't be (e.g. an older project
    // that saved scene.world at the project root) — prompt for a new path
    // before writing.
    if (worldPath.empty() || !isPathUnder(worldPath, assetsDir))
    {
        kString chosen = promptWorldSavePath();
        if (chosen.empty())
            return; // cancelled or rejected
        worldPath = chosen;
    }

    // Persist the project's physics layers into the world file so the
    // standalone runtime can restore same-layer-only collision.
    world->setPhysicsLayers(layerSettings.layers);
    json data = world->serialize(1); // skip editor scene at index 0

    // Stamp the editor-side Ingame UI (.ui) object links onto their objects.
    injectUiRefsIntoJson(data);

    // --- Save terrain data alongside the world ---
    // Walk the scene graph to find terrain meshes and save their
    // height/splat data to Library/Terrains/{uuid}.{height,splat}.
    // This works regardless of whether terrainManager is available.
    {
        fs::path libDir = projectPath / "Library" / "Terrains";
        fs::create_directories(libDir);

        std::function<void(kObject *)> walkTerrain = [&](kObject *node)
        {
            kMesh *mesh = dynamic_cast<kMesh *>(node);
            if (mesh && mesh->getSerializeType() == "terrain")
            {
                kString uuid = mesh->getUuid();
                if (!uuid.empty())
                {
                    // Try terrainManager first (has full kTerrain with height data)
                    bool saved = false;
                    if (terrainManager)
                    {
                        for (const auto &pair : terrainManager->getTiles())
                        {
                            const kTerrain *tile = pair.second.get();
                            if (tile && tile->getMesh() && tile->getMesh()->getUuid() == uuid)
                            {
                                tile->saveHeightData((libDir / (uuid + ".height")).string());
                                tile->saveSplatMap((libDir / (uuid + ".splat")).string());
                                saved = true;
                                break;
                            }
                        }
                    }
                    if (!saved)
                    {
                        // Fallback: reconstruct height from mesh vertex Y positions.
                        // This is a lossy reconstruction from the GPU vertex data
                        // but preserves the visible shape.
                        const auto &verts = mesh->getVerticesRef();
                        int count = static_cast<int>(verts.size());
                        // Determine height resolution (nearest perfect square)
                        int hRes = static_cast<int>(std::sqrt(static_cast<float>(count)));
                        if (hRes * hRes == count && hRes > 1)
                        {
                            std::vector<float> heights(count);
                            for (int i = 0; i < count; ++i)
                                heights[i] = verts[i].y;
                            std::ofstream hFile(libDir / (uuid + ".height"), std::ios::binary);
                            if (hFile.is_open())
                            {
                                hFile.write(reinterpret_cast<const char *>(heights.data()),
                                            heights.size() * sizeof(float));
                                hFile.close();
                            }
                            // Save a blank splat map (all zeros)
                            std::vector<unsigned char> splat(static_cast<size_t>(hRes) * hRes * 4, 0);
                            stbi_write_png((libDir / (uuid + ".splat")).string().c_str(),
                                           hRes, hRes, 4, splat.data(), hRes * 4);
                        }
                    }

                }
            }
            for (kObject *child : node->getChildren())
                walkTerrain(child);
        };

        for (kScene *s : world->getScenes())
            if (s && s->getRootNode())
                walkTerrain(s->getRootNode());
    }

    // Persist the editor viewport camera so reopening the world restores the
    // same viewpoint (position, orientation, and orbit framing state).
    if (editorCamera)
    {
        kVec3 cp = editorCamera->getPosition();
        kQuat cq = editorCamera->getRotation();
        data["editor_camera"] = {
            {"position", {{"x", cp.x}, {"y", cp.y}, {"z", cp.z}}},
            {"rotation", {{"x", cq.x}, {"y", cq.y}, {"z", cq.z}, {"w", cq.w}}},
            {"orbit_pivot", {{"x", editorCamOrbitPivot.x}, {"y", editorCamOrbitPivot.y}, {"z", editorCamOrbitPivot.z}}},
            {"orbit_distance", editorCamOrbitDistance},
            {"fov", editorCamera->getFOV()},
            {"near_clip", editorCamera->getNearClip()},
            {"far_clip", editorCamera->getFarClip()},
        };
    }

    try
    {
        std::ofstream f(worldPath);
        if (!f.is_open())
        {
            std::cerr << "saveWorld: cannot open " << worldPath << "\n";
            return;
        }
        f << data.dump(4);
        // Also save terrain data if the terrain panel is active
        if (panelTerrain)
            panelTerrain->saveProjectTerrains();

        projectSaved = true;
        worldName = worldPath.stem().string();
        refreshWindowTitle();
        // Record this as the last-opened world so reopening the project
        // auto-loads it.
        saveProjectConfig();
        // Refresh the project panel so the .world file appears immediately.
        if (panelProject)
            panelProject->triggerRefresh();
        std::cout << "World saved: " << worldPath << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "saveWorld: " << e.what() << "\n";
    }
}

void Manager::saveWorldAs(const kString &path)
{
    if (!projectOpened || !world)
        return;

    fs::path p = path;
    if (p.extension() != ".world")
        p += ".world";

    if (!isPathUnder(p, projectPath / "Assets"))
    {
        showingMessageBox = true;
        pfd::message(
            "Invalid Save Location",
            "Worlds can only be saved inside the project's Assets folder.\n\n"
            "Refused:  " +
                p.string(),
            pfd::choice::ok, pfd::icon::warning)
            .result();
        showingMessageBox = false;
        return;
    }

    worldPath = p;
    saveWorld();
}

void Manager::saveWorldAs()
{
    if (!projectOpened || !world)
        return;

    kString chosen = promptWorldSavePath();
    if (chosen.empty())
        return;
    worldPath = chosen;
    saveWorld();
}

static void applyLightIconLoad(kLight *light, kAssetManager *am, const char *gizmoResource)
{
    kShader *shader = am->loadGlslFromResource("SHADER_ICON");
    kMaterial *mat = am->createMaterial(shader);
    mat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND); // blend so the icon's drop shadow / soft edges show
    kTexture2D *tex = am->loadTexture2DFromResource(gizmoResource, "albedoMap",
                                                    kTextureFormat::TEX_FORMAT_RGBA);
    mat->addTexture(tex);
    light->setMaterial(mat);
}

// Resolve an import-child sub-mesh by the index-path produced by
// kObject::serialize (collectSubmeshMaterials). Mirrors that traversal: count
// only import-child siblings, descend per dotted segment.
static kObject *resolveSubmeshByPath(kObject *node, const std::vector<int> &path, size_t depth)
{
    if (!node || depth >= path.size())
        return nullptr;
    int idx = 0;
    for (kObject *c : node->getChildren())
    {
        if (!c->getImportChild())
            continue;
        if (idx == path[depth])
            return (depth + 1 == path.size()) ? c
                                              : resolveSubmeshByPath(c, path, depth + 1);
        ++idx;
    }
    return nullptr;
}

static kObject *loadObjectFromJson(const json &obj, kScene *scene, kWorld *world,
                                   kAssetManager *am, const fs::path &projectPath,
                                   kCamera *editorCamera, kObject *parent,
                                   kTerrainManager *terrainMgr)
{
    std::string type = obj.value("type", "object");
    std::string uuid = obj.value("uuid", "");
    std::string name = obj.value("name", "");
    bool active = obj.value("active", true);

    auto readVec3xyz = [&](const json &j, const char *key, kVec3 def = kVec3(0)) -> kVec3
    {
        if (!j.contains(key))
            return def;
        const auto &v = j[key];
        return kVec3(v.value("x", def.x), v.value("y", def.y), v.value("z", def.z));
    };
    auto readVec3rgb = [&](const json &j, const char *key, kVec3 def = kVec3(1)) -> kVec3
    {
        if (!j.contains(key))
            return def;
        const auto &v = j[key];
        return kVec3(v.value("r", def.x), v.value("g", def.y), v.value("b", def.z));
    };

    kVec3 pos = readVec3xyz(obj, "position");
    kVec3 rotEu = readVec3xyz(obj, "rotation");
    kVec3 scale = readVec3xyz(obj, "scale", kVec3(1.0f));

    // Top-level objects attach to the scene root via scene->add*; nested objects
    // (parent != nullptr) are created standalone and parented manually so they
    // don't end up double-tracked.
    bool topLevel = (parent == nullptr);

    kObject *result = nullptr;

    if (type == "mesh" || type == "terrain")
    {
        std::string refName = obj.value("reference", "");
        std::string fileName = obj.value("file_name", "");
        std::string primitive = obj.value("primitive", "");

        fs::path meshPath;
        if (!refName.empty())
            meshPath = projectPath / "Library" / "ImportedAssets" / (refName + ".glb");
        else if (!fileName.empty())
            meshPath = fs::path(fileName);

        kMesh *mesh = nullptr;
        if (!meshPath.empty() && fs::exists(meshPath))
        {
            mesh = am->loadMesh(meshPath.string());
            // Give the model (and its sub-meshes) the default material so it is
            // never invisible after a load. A stored material_uuid, if any, is
            // re-applied afterwards by reapplyStoredMaterials and overrides this.
            if (mesh && am)
                applyDefaultMaterial(mesh, am);
        }

        // Procedurally-generated primitive (cube / sphere / etc.) — used by
        // res/default.world to seed a startup scene without shipping a model
        // file. Only kicks in when no asset file is present.
        if (!mesh && !primitive.empty())
        {
            if (primitive == "cube")
                mesh = kMeshGenerator::generateCube();
            else if (primitive == "sphere")
                mesh = kMeshGenerator::generateSphere();
            else if (primitive == "cylinder")
                mesh = kMeshGenerator::generateCylinder();
            else if (primitive == "capsule")
                mesh = kMeshGenerator::generateCapsule();
            else if (primitive == "plane")
                mesh = kMeshGenerator::generatePlane();
            if (mesh && am)
                applyDefaultMaterial(mesh, am);
        }

        if (!mesh)
        {
            mesh = new kMesh();
            std::cerr << "loadObjectFromJson: mesh file not found for '" << name << "'\n";
        }

        mesh->setName(name);
        if (!refName.empty())
            mesh->setRefName(refName); // keep asset link for save / re-import
        mesh->setActive(active);
        mesh->setStatic(obj.value("static", false));  // was serialized but never restored
        mesh->setVisible(obj.value("visible", true)); // same: restore on load
        mesh->setCastShadow(obj.value("cast_shadow", true));
        mesh->setReceiveShadow(obj.value("receive_shadow", true));
        if (topLevel)
        {
            scene->addMesh(mesh, uuid);
        }
        else
        {
            mesh->setUuid(uuid.empty() ? generateUuid() : uuid);
            mesh->setParent(parent);
        }

        // For terrain tiles, create a kTerrain object instead of just a mesh.
        if (type == "terrain")
        {
            mesh->setSerializeType("terrain");

            int gx = obj.value("gridX", 0);
            int gz = obj.value("gridZ", 0);

            // Remove the empty mesh we just created (terrain will create its own)
            scene->removeMesh(mesh);
            delete mesh;
            mesh = nullptr;

            // terrainMgr must be non-null at this point — loadWorld pre-initialises
            // it before calling loadObjectFromJson if any terrain objects exist.
            if (!terrainMgr)
            {
                std::cerr << "loadObjectFromJson: terrainMgr is null, cannot create terrain tile\n";
                result = nullptr;
                return result;
            }

            kTerrain *tile = terrainMgr->createTile(gx, gz);
            if (tile)
            {
                // Load height/splat binary data from Library/Terrains/
                fs::path libDir = projectPath / "Library" / "Terrains";
                kString hFile = libDir.string() + "/" + obj.value("heightFile", "");
                kString sFile = libDir.string() + "/" + obj.value("splatFile", "");
                if (fs::exists(hFile))
                    tile->loadHeightData(hFile);
                if (fs::exists(sFile))
                    tile->loadSplatMap(sFile);

                tile->reload(true);

                mesh = tile->getMesh();
                if (mesh)
                {
                    // Preserve the original UUID from the .world JSON so the
                    // mesh keeps its identity across save/load cycles.
                    if (!uuid.empty())
                        mesh->setUuid(uuid);

                    mesh->setSerializeType("terrain");
                    mesh->setName(name);
                    mesh->setActive(active);
                    // Use force-setters for terrain too — the static flag on the
                    // original mesh (line 3978) may have been set before the
                    // terrain branch deleted it and created a replacement.
                    mesh->setPositionForced(pos);
                    mesh->setRotationForced(kQuat(rotEu));
                    mesh->setScaleForced(scale);

                    // Restore the material UUID from the saved world JSON.
                    // Without this the early return (result = mesh; return result;)
                    // skips the generic material_uuid handling below, and the terrain
                    // mesh would lose its assigned material on reload.
                    if (obj.contains("material_uuid"))
                        mesh->setMaterialUuid(obj["material_uuid"].get<std::string>());
                }
            }
            result = mesh;
            return result;
        }

        // Give the model's import-derived sub-meshes UUIDs so they show
        // uniquely in the hierarchy and can be selected (they remain excluded
        // from serialization via the import-child flag).
        assignImportChildUuidsRec(mesh);
        result = mesh;
    }
    else if (type == "light")
    {
        std::string ltStr = obj.value("light_type", "sun");
        kVec3 diff = readVec3rgb(obj, "diffuse", kVec3(1));
        kVec3 spec = readVec3rgb(obj, "specular", kVec3(1));
        float power = obj.value("power", 1.0f);
        float constant = obj.value("constant", 1.0f);
        float linear = obj.value("linear", 0.7f);
        float quadratic = obj.value("quadratic", 1.8f);
        kVec3 dir = readVec3xyz(obj, "direction", kVec3(0, -1, 0));
        float cutOff = obj.value("cut_off", glm::cos(glm::radians(15.0f)));
        float outerCutOff = obj.value("outer_cut_off", glm::cos(glm::radians(20.0f)));

        kLight *light = nullptr;
        const char *gizmoRes = "GIZMO_SUN_LIGHT";

        if (topLevel)
        {
            if (ltStr == "point")
            {
                light = scene->addPointLight(pos, diff, spec, uuid);
                gizmoRes = "GIZMO_POINT_LIGHT";
            }
            else if (ltStr == "spot")
            {
                light = scene->addSpotLight(pos, diff, spec, uuid);
                gizmoRes = "GIZMO_SPOT_LIGHT";
            }
            else
            {
                light = scene->addSunLight(pos, dir, diff, spec, uuid);
                gizmoRes = "GIZMO_SUN_LIGHT";
            }
        }
        else
        {
            light = new kLight();
            if (ltStr == "point")
            {
                light->setLightType(kLightType::LIGHT_TYPE_POINT);
                gizmoRes = "GIZMO_POINT_LIGHT";
            }
            else if (ltStr == "spot")
            {
                light->setLightType(kLightType::LIGHT_TYPE_SPOT);
                gizmoRes = "GIZMO_SPOT_LIGHT";
            }
            else
            {
                light->setLightType(kLightType::LIGHT_TYPE_SUN);
                gizmoRes = "GIZMO_SUN_LIGHT";
            }
            light->setDiffuseColor(diff);
            light->setSpecularColor(spec);
            light->setUuid(uuid.empty() ? generateUuid() : uuid);
            light->setParent(parent);
        }

        light->setName(name);
        light->setActive(active);
        light->setPower(power);
        light->setConstant(constant);
        light->setLinear(linear);
        light->setQuadratic(quadratic);
        light->setDirection(dir);
        light->setCutOff(cutOff);
        light->setOuterCutOff(outerCutOff);
        if (am)
            applyLightIconLoad(light, am, gizmoRes);

        result = light;
    }
    else if (type == "camera")
    {
        std::string camTypeStr = obj.value("camera_type", "free");
        kCameraType camType = (camTypeStr == "locked") ? kCameraType::CAMERA_TYPE_LOCKED
                                                       : kCameraType::CAMERA_TYPE_FREE;
        kVec3 lookAt = readVec3xyz(obj, "look_at");
        float fov = obj.value("fov", 60.0f);
        float nearClip = obj.value("near_clip", 0.1f);
        float farClip = obj.value("far_clip", 1000.0f);

        kCamera *cam = new kCamera();
        cam->setName(name);
        cam->setActive(active);
        cam->setCameraType(camType);
        cam->setLookAt(lookAt);
        cam->setFOV(fov);
        cam->setNearClip(nearClip);
        cam->setFarClip(farClip);

        cam->setSceneUuid(obj.value("scene_uuid", std::string("")));

        if (topLevel)
        {
            scene->addObject(cam, uuid);
        }
        else
        {
            cam->setUuid(uuid.empty() ? generateUuid() : uuid);
            cam->setParent(parent);
        }
        // Cameras always register with the world so they can be selected as game cameras.
        world->addCamera(cam, cam->getUuid());

        // Editor-side gizmo (icon billboard) — the renderer auto-hides it when
        // the camera is also the active view camera.
        if (am)
            applyCameraIcon(cam, am);

        result = cam;
    }
    else if (type == "audio")
    {
        kObject *audioObj = new kObject();
        audioObj->setType(kNodeType::NODE_TYPE_AUDIO);
        audioObj->setName(name);
        audioObj->setActive(active);
        audioObj->setStatic(obj.value("static", false));
        if (topLevel)
        {
            scene->addObject(audioObj, uuid);
        }
        else
        {
            audioObj->setUuid(uuid.empty() ? generateUuid() : uuid);
            audioObj->setParent(parent);
        }
        if (am)
        {
            // Determine icon from JSON before components are loaded:
            // a pure listener (has audio_listeners but no audio_sources)
            // should use the listener icon, not the generic audio icon.
            bool hasListeners = obj.contains("audio_listeners") &&
                                obj["audio_listeners"].is_array() &&
                                !obj["audio_listeners"].empty();
            bool hasSources = obj.contains("audio_sources") &&
                              obj["audio_sources"].is_array() &&
                              !obj["audio_sources"].empty();

            if (hasListeners && !hasSources)
                applyAudioListenerIcon(audioObj, am);
            else
                applyAudioIcon(audioObj, am);
        }
        result = audioObj;
    }
    else if (type == "decal")
    {
        kDecal *decal = new kDecal();
        decal->setName(name);
        decal->setActive(active);
        decal->setStatic(obj.value("static", false));
        decal->setShaderType(obj.value("decal_shader", std::string("flat")));
        decal->setSurfaceOffset(obj.value("decal_offset", 0.01f));
        if (topLevel)
        {
            scene->addObject(decal, uuid);
        }
        else
        {
            decal->setUuid(uuid.empty() ? generateUuid() : uuid);
            decal->setParent(parent);
        }
        // No .mat assigned (no material_uuid key): rebuild the built-in default
        // material from the saved shader type so the decal is visible right away.
        if (am && !obj.contains("material_uuid"))
            applyDecalShaderMaterial(decal, am, decal->getShaderType());
        result = decal;
    }
    else
    {
        // Fallback: if the saved type is "object" but the JSON carries
        // audio-source data (can happen when the engine library was not
        // rebuilt after the NODE_TYPE_AUDIO serialisation was added),
        // promote the object to a proper audio emitter so the inspector
        // shows the Audio Source section and the hierarchy uses the
        // audio icon.
        bool hasAudioSources = obj.contains("audio_sources") &&
                               obj["audio_sources"].is_array() &&
                               !obj["audio_sources"].empty();

        if (hasAudioSources)
        {
            kObject *audioObj = new kObject();
            audioObj->setType(kNodeType::NODE_TYPE_AUDIO);
            audioObj->setName(name);
            audioObj->setActive(active);
            audioObj->setStatic(obj.value("static", false));
            if (topLevel)
                scene->addObject(audioObj, uuid);
            else
            {
                audioObj->setUuid(uuid.empty() ? generateUuid() : uuid);
                audioObj->setParent(parent);
            }
            if (am)
                applyAudioIcon(audioObj, am);
            result = audioObj;
        }
        else
        {
            kObject *empty = new kObject();
            empty->setName(name);
            empty->setActive(active);
            empty->setStatic(obj.value("static", false));
            if (topLevel)
                scene->addObject(empty, uuid);
            else
            {
                empty->setUuid(uuid.empty() ? generateUuid() : uuid);
                empty->setParent(parent);
            }
            result = empty;
        }
    }

    if (result)
    {
        // Use force-setters so the initial transform is always restored on load,
        // even when the object is marked as static (the static guard only blocks
        // runtime modifications — deserialization must always succeed).
        result->setPositionForced(pos);
        result->setRotationForced(kQuat(glm::radians(rotEu)));
        result->setScaleForced(scale);

        // User-defined tag (like Unity's tags).
        if (obj.contains("tag") && obj["tag"].is_string())
            result->setTag(obj["tag"].get<std::string>());

        // Prefab linkage — only set when present in JSON, otherwise stays empty.
        if (obj.contains("prefab_ref"))
            result->setPrefabRef(obj["prefab_ref"].get<std::string>());
        if (obj.contains("template_uuid"))
            result->setTemplateUuid(obj["template_uuid"].get<std::string>());

        // Assigned material asset UUID. Only the reference is restored here; the
        // runtime material is rebuilt afterwards by Manager::reapplyStoredMaterials
        // (which has the fileMap needed to resolve the UUID to a .mat path).
        if (obj.contains("material_uuid"))
            result->setMaterialUuid(obj["material_uuid"].get<std::string>());

        // Assigned animator asset UUID.
        if (obj.contains("animator_ref"))
            result->setAnimatorRef(obj["animator_ref"].get<std::string>());

        // Per-sub-mesh overrides for import-derived sub-meshes. Each entry holds
        // only the fields the user changed (material, active, visible, static,
        // shadows). The material UUID is applied later by reapplyStoredMaterials
        // (it recurses import-children); the flags are applied here.
        if (obj.contains("submesh_materials") && obj["submesh_materials"].is_array())
        {
            for (const json &sm : obj["submesh_materials"])
            {
                std::string ps = sm.value("path", std::string(""));
                if (ps.empty())
                    continue;

                std::vector<int> path;
                std::stringstream ss(ps);
                std::string seg;
                while (std::getline(ss, seg, '.'))
                    if (!seg.empty())
                        path.push_back(std::stoi(seg));

                kObject *sub = resolveSubmeshByPath(result, path, 0);
                if (!sub)
                    continue;

                if (sm.contains("material_uuid"))
                    sub->setMaterialUuid(sm["material_uuid"].get<std::string>());
                if (sm.contains("active"))
                    sub->setActive(sm["active"].get<bool>());
                if (sm.contains("static"))
                    sub->setStatic(sm["static"].get<bool>());
                if (kMesh *subMesh = dynamic_cast<kMesh *>(sub))
                {
                    if (sm.contains("visible"))
                        subMesh->setVisible(sm["visible"].get<bool>());
                    if (sm.contains("cast_shadow"))
                        subMesh->setCastShadow(sm["cast_shadow"].get<bool>());
                    if (sm.contains("receive_shadow"))
                        subMesh->setReceiveShadow(sm["receive_shadow"].get<bool>());
                }
            }
        }

        // Physics body descriptor — fields mirror kPhysicsObjectDesc; missing
        // fields fall back to descriptor defaults.
        if (obj.contains("physics") && obj["physics"].is_object())
        {
            const json &phys = obj["physics"];
            kPhysicsObjectDesc &desc = result->getPhysicsDesc();
            desc.shape.type = (kPhysicsShapeType)phys.value("shape_type", 0);
            if (phys.contains("half_extents") && phys["half_extents"].is_object())
            {
                const auto &he = phys["half_extents"];
                desc.shape.halfExtents = kVec3(
                    he.value("x", 0.5f), he.value("y", 0.5f), he.value("z", 0.5f));
            }
            desc.shape.radius = phys.value("radius", 0.5f);
            desc.shape.height = phys.value("height", 1.0f);
            desc.type = (kPhysicsObjectType)phys.value("body_type", 0);
            desc.mass = phys.value("mass", 1.0f);
            desc.friction = phys.value("friction", 0.5f);
            desc.restitution = phys.value("restitution", 0.0f);
            desc.linearDamping = phys.value("linear_damping", 0.05f);
            desc.angularDamping = phys.value("angular_damping", 0.05f);
            desc.gravityFactor = phys.value("gravity_factor", 1.0f);
            desc.layer = phys.value("layer", std::string("Default"));
            result->setHasPhysicsDesc(true);
        }

        // Character controller descriptor.
        if (obj.contains("character") && obj["character"].is_object())
        {
            const json &ch = obj["character"];
            kCharacterControllerDesc &cd = result->getCharacterDesc();
            cd.radius = ch.value("radius", 0.3f);
            cd.height = ch.value("height", 1.8f);
            cd.mass = ch.value("mass", 80.0f);
            cd.friction = ch.value("friction", 0.5f);
            cd.gravityFactor = ch.value("gravity_factor", 1.0f);
            cd.slopeLimit = ch.value("slope_limit", 45.0f);
            cd.stepHeight = ch.value("step_height", 0.3f);
            cd.layer = ch.value("layer", std::string("Default"));
            result->setHasCharacterDesc(true);
        }

        // Navigation surface descriptor (bake settings only).
        if (obj.contains("navmesh_surface") && obj["navmesh_surface"].is_object())
        {
            const json &nm = obj["navmesh_surface"];
            kNavMeshDesc &nd = result->getNavMeshDesc();
            nd.useArea = nm.value("use_area", false);
            if (nm.contains("area_size") && nm["area_size"].is_object())
            {
                const auto &as = nm["area_size"];
                nd.areaSize = kVec3(as.value("x", 20.0f), as.value("y", 10.0f), as.value("z", 20.0f));
            }
            nd.config.cellSize = nm.value("cell_size", 0.3f);
            nd.config.cellHeight = nm.value("cell_height", 0.2f);
            nd.config.agentHeight = nm.value("agent_height", 2.0f);
            nd.config.agentRadius = nm.value("agent_radius", 0.6f);
            nd.config.agentMaxClimb = nm.value("agent_max_climb", 0.9f);
            nd.config.agentMaxSlope = nm.value("agent_max_slope", 45.0f);
            nd.config.tileSize = nm.value("tile_size", 48.0f);
            result->setHasNavMeshDesc(true);
        }

        // Script components — restore each AngelScript attachment. The runtime
        // module is built later (on Play) by kWorld::startScripts().
        if (obj.contains("script") && obj["script"].is_array())
        {
            for (const auto &sj : obj["script"])
            {
                kScript s;
                s.uuid = sj.value("uuid", generateUuid());
                s.scriptUuid = sj.value("script_uuid", std::string(""));
                s.fileName = sj.value("file_name", std::string(""));
                s.checksum = sj.value("checksum", std::string(""));
                s.isActive = sj.value("active", true);

                if (sj.contains("variables") && sj["variables"].is_array())
                {
                    for (const auto &vj : sj["variables"])
                    {
                        kScriptVarBinding vb;
                        vb.name      = vj.value("name", std::string(""));
                        vb.typeName  = vj.value("type", std::string(""));
                        vb.valueStr  = vj.value("value_str", std::string(""));
                        vb.valueBool = vj.value("value_bool", false);
                        vb.assigned  = vj.value("assigned", false);
                        if (vj.contains("value") && vj["value"].is_array() &&
                            vj["value"].size() == 3)
                            for (int i = 0; i < 3; ++i)
                                vb.valueFloat[i] = vj["value"][i].get<float>();
                        s.variableBindings.push_back(vb);
                    }
                }

                result->addScript(s);
            }
        }

        // Audio sources — restore each attached audio source descriptor.
        if (obj.contains("audio_sources") && obj["audio_sources"].is_array())
        {
            for (const auto &aj : obj["audio_sources"])
            {
                kAudioSource as;
                as.uuid         = aj.value("uuid", generateUuid());
                as.name         = aj.value("name", std::string("Audio Source"));
                as.audioFile    = aj.value("audio_file", std::string(""));
                as.isActive     = aj.value("active", true);
                as.playOnAwake  = aj.value("play_on_awake", false);
                as.loop         = aj.value("loop", false);
                as.volume       = aj.value("volume", 1.0f);
                as.pitch        = aj.value("pitch", 1.0f);
                as.spatialize   = aj.value("spatialize", true);
                as.minDistance  = aj.value("min_distance", 1.0f);
                as.maxDistance  = aj.value("max_distance", 100.0f);
                result->addAudioSource(as);
            }
        }

        // Audio listeners — restore each attached audio listener descriptor.
        if (obj.contains("audio_listeners") && obj["audio_listeners"].is_array())
        {
            for (const auto &lj : obj["audio_listeners"])
            {
                kAudioListener al;
                al.uuid    = lj.value("uuid", generateUuid());
                al.isActive = lj.value("active", true);
                result->addAudioListener(al);
            }
        }

        // Particle systems — restore each attached particle descriptor.
        if (obj.contains("particle") && obj["particle"].is_array())
        {
            for (const auto &pj : obj["particle"])
            {
                kParticle p;
                p.uuid           = pj.value("uuid", generateUuid());
                p.name           = pj.value("name", std::string("Particle System"));
                p.isActive       = pj.value("active", true);
                p.looping        = pj.value("looping", true);
                p.maxParticles   = pj.value("max_particles", 100);
                p.emissionRate   = pj.value("emission_rate", 10.0f);
                p.lifetime       = pj.value("lifetime", 2.0f);
                p.gravityScale   = pj.value("gravity_scale", 1.0f);
                p.startSpeed     = pj.value("start_speed", 1.0f);
                p.sizeStart      = pj.value("size_start", 0.1f);
                p.sizeEnd        = pj.value("size_end", 0.0f);
                p.emissionShape  = (kParticle::EmissionShape)pj.value("emission_shape", 0);
                p.texturePath    = pj.value("texture_path", std::string(""));

                // Nested vec3/vec4 objects
                if (pj.contains("start_velocity") && pj["start_velocity"].is_object())
                {
                    const auto &sv = pj["start_velocity"];
                    p.startVelocity = kVec3(sv.value("x", 0.0f), sv.value("y", 1.0f), sv.value("z", 0.0f));
                }
                if (pj.contains("velocity_variance") && pj["velocity_variance"].is_object())
                {
                    const auto &vv = pj["velocity_variance"];
                    p.velocityVariance = kVec3(vv.value("x", 0.1f), vv.value("y", 0.1f), vv.value("z", 0.1f));
                }
                if (pj.contains("color_start") && pj["color_start"].is_object())
                {
                    const auto &cs = pj["color_start"];
                    p.colorStart = kVec4(cs.value("r", 1.0f), cs.value("g", 1.0f), cs.value("b", 1.0f), cs.value("a", 1.0f));
                }
                if (pj.contains("color_end") && pj["color_end"].is_object())
                {
                    const auto &ce = pj["color_end"];
                    p.colorEnd = kVec4(ce.value("r", 1.0f), ce.value("g", 1.0f), ce.value("b", 1.0f), ce.value("a", 0.0f));
                }
                if (pj.contains("shape_size") && pj["shape_size"].is_object())
                {
                    const auto &ss = pj["shape_size"];
                    p.shapeSize = kVec3(ss.value("x", 0.5f), ss.value("y", 0.5f), ss.value("z", 0.5f));
                }

                result->addParticle(p);
            }
        }

        // Recursively load children, parented to this node.
        if (obj.contains("children") && obj["children"].is_array())
        {
            for (const auto &child : obj["children"])
                loadObjectFromJson(child, scene, world, am, projectPath, editorCamera, result);
        }
    }

    // Restore the editor-side Ingame UI (.ui) link carried by this object.
    if (result && obj.contains("ui") && obj["ui"].is_string())
    {
        kString uiUuid = obj["ui"].get<std::string>();
        if (!uiUuid.empty())
            g_objectUiAssets[result->getUuid()] = uiUuid;
    }

    return result;
}

void Manager::loadWorld(const kString &path)
{
    if (!projectOpened || !world)
        return;

    fs::path loadPath = path;
    if (!fs::exists(loadPath))
    {
        std::cerr << "loadWorld: file not found: " << path << "\n";
        return;
    }

    // Old baked nav meshes are keyed by now-stale object UUIDs — drop them.
    clearAllNavMeshes();
    stopAnimators();

    // Rebuilt from the world file below; drop stale Ingame UI (.ui) links.
    clearObjectUiAssets();

    json data;
    try
    {
        std::ifstream f(loadPath);
        data = json::parse(f);
    }
    catch (const std::exception &e)
    {
        std::cerr << "loadWorld: parse error: " << e.what() << "\n";
        return;
    }

    // Remove non-editor cameras from world first (before deleting scene objects)
    {
        auto cams = world->getCameras();
        for (kCamera *c : cams)
            if (c != editorCamera)
                world->removeCamera(c);
    }

    // Clear game scenes (keep editor scene at index 0)
    {
        auto scenes = world->getScenes();
        for (size_t i = 1; i < scenes.size(); ++i)
        {
            kScene *s = scenes[i];
            if (s && s->getRootNode())
            {
                for (kObject *child : s->getRootNode()->getChildren())
                    deleteObjectRecursive(child);
                delete s->getRootNode();
            }
            world->removeScene(s);
            delete s;
        }
    }
    defaultGameCamera = nullptr;
    selectedObject = nullptr;
    selectedScene = nullptr;
    selectedObjects.clear();

    kAssetManager *am = getAssetManager();

    // Restore the physics layers stored in the world file (fall back to the
    // project's layer settings when the world predates layer support).
    if (data.contains("physics_layers") && data["physics_layers"].is_array())
    {
        std::vector<std::string> layers;
        for (const auto &l : data["physics_layers"])
            if (l.is_string())
                layers.push_back(l.get<std::string>());
        if (!layers.empty())
        {
            layerSettings.layers = layers;
            saveLayerSettings();
        }
    }
    world->setPhysicsLayers(layerSettings.layers);

    if (!data.contains("scenes") || !data["scenes"].is_array())
    {
        std::cerr << "loadWorld: no scenes array in file\n";
        return;
    }

    for (const auto &sceneJson : data["scenes"])
    {
        std::string sceneUuid = sceneJson.value("uuid", "");
        std::string sceneName = sceneJson.value("name", "Scene");
        bool sceneActive = sceneJson.value("active", true);

        kScene *scene = world->createScene(sceneName, sceneUuid);
        scene->setActive(sceneActive);

        if (sceneJson.contains("ambient_light"))
        {
            const auto &al = sceneJson["ambient_light"];
            scene->setAmbientLightColor(kVec3(al.value("r", 0.1f), al.value("g", 0.1f), al.value("b", 0.1f)));
        }
        scene->setShadowsEnabled(sceneJson.value("shadows_enabled", true));
        scene->setShadowBias(sceneJson.value("shadow_bias", 0.0008f));
        scene->setShadowNormalBias(sceneJson.value("shadow_normal_bias", 0.003f));
        scene->setShadowMapResolution(sceneJson.value("shadow_map_resolution", 2048));
        scene->setShadowSoftness(sceneJson.value("shadow_softness", 1.5f));
        scene->setSkyboxAmbientEnabled(sceneJson.value("skybox_ambient_enabled", false));
        scene->setSkyboxAmbientStrength(sceneJson.value("skybox_ambient_strength", 1.0f));

        // Ensure every loaded scene has the default skybox so the editor view is
        // never sky-less (the .world doesn't yet persist a custom skybox).
        if (scene->getSkyboxMaterial() == nullptr)
            applyDefaultSkybox(scene);

        // Pre-initialise the terrain manager if the scene contains terrain
        // objects.  This must happen BEFORE the object loop so that
        // loadObjectFromJson receives a non-null terrainMgr pointer (pass-by-
        // value) and can create kTerrain tiles through it.
        if (!terrainManager && sceneJson.contains("objects") && sceneJson["objects"].is_array())
        {
            for (const auto &objJson : sceneJson["objects"])
            {
                if (objJson.value("type", "") == "terrain")
                {
                    terrainManager = new kTerrainManager();
                    terrainManager->init(scene, am,
                                         objJson.value("worldSize", 256.0f),
                                         objJson.value("heightRes", 513));
                    break;
                }
            }
        }

        if (sceneJson.contains("objects") && sceneJson["objects"].is_array())
        {
            for (const auto &objJson : sceneJson["objects"])
            {
                loadObjectFromJson(objJson, scene, world, am, projectPath, editorCamera, nullptr, terrainManager);
            }
        }

        // Use this scene as the main game scene
        setScene(scene);
    }

    // Second pass: rebuild runtime materials from the per-object material UUIDs
    // restored during load. Done here (not in loadObjectFromJson) because it
    // needs fileMap to resolve a UUID to its .mat path.
    reapplyStoredMaterials();

    // Restore the editor viewport camera to where it was when saved. Orbit
    // pivot/distance are staged for main.cpp via editorCamLoadPending so its
    // interaction state matches the restored pose.
    if (editorCamera && data.contains("editor_camera") && data["editor_camera"].is_object())
    {
        const json &ec = data["editor_camera"];
        if (ec.contains("position"))
        {
            const auto &p = ec["position"];
            editorCamera->setPosition(kVec3(p.value("x", 0.0f), p.value("y", 0.0f), p.value("z", 0.0f)));
        }
        if (ec.contains("rotation"))
        {
            const auto &r = ec["rotation"];
            // glm::quat ctor is (w, x, y, z).
            editorCamera->setRotation(kQuat(
                r.value("w", 1.0f), r.value("x", 0.0f), r.value("y", 0.0f), r.value("z", 0.0f)));
        }
        if (ec.contains("orbit_pivot"))
        {
            const auto &op = ec["orbit_pivot"];
            editorCamOrbitPivot = kVec3(op.value("x", 0.0f), op.value("y", 0.0f), op.value("z", 0.0f));
        }
        editorCamOrbitDistance = ec.value("orbit_distance", editorCamOrbitDistance);
        if (ec.contains("fov"))
            editorCamera->setFOV(ec.value("fov", 60.0f));
        if (ec.contains("near_clip"))
            editorCamera->setNearClip(ec.value("near_clip", 0.1f));
        if (ec.contains("far_clip"))
            editorCamera->setFarClip(ec.value("far_clip", 10000.0f));
        editorCamLoadPending = true;
    }

    worldName = loadPath.stem().string();
    worldPath = loadPath;
    projectSaved = true;
    refreshWindowTitle();

    // Walk all loaded terrain tiles and load their height/splat data.
    // (The kTerrain objects were created by loadObjectFromJson but the
    // data files need to be loaded from Library/Terrains/.)
    if (terrainManager && projectOpened)
    {
        for (auto &pair : terrainManager->getTiles())
        {
            kTerrain *tile = pair.second.get();
            if (tile && tile->getMesh())
            {
                fs::path libDir = projectPath / "Library" / "Terrains";
                kString uuid = tile->getMesh()->getUuid();
                kString hFile = libDir.string() + "/" + uuid + ".height";
                kString sFile = libDir.string() + "/" + uuid + ".splat";
                if (fs::exists(hFile))
                    tile->loadHeightData(hFile);
                if (fs::exists(sFile))
                    tile->loadSplatMap(sFile);

                // Rebuild the mesh so the GPU vertex buffer reflects the loaded height data.
                // loadHeightData only updates the CPU array; reload() builds the GPU mesh.
                tile->reload(true);
            }
        }
        if (getRenderer())
            getRenderer()->setOctreeDirty();
    }

    if (panelHierarchy)
        panelHierarchy->refreshList();

    std::cout << "World loaded: " << loadPath << "\n";
}

void Manager::loadDefaultWorkspace()
{
    if (!gui)
        return;

    HRSRC hRes = FindResource(NULL, "LAYOUT_DEFAULT", RT_RCDATA);
    if (!hRes)
        return;

    HGLOBAL hData = LoadResource(NULL, hRes);
    DWORD size = SizeofResource(NULL, hRes);
    const char *data = static_cast<const char *>(LockResource(hData));
    if (data && size > 0)
        gui->loadIniSettingsFromMemory(data, size);
}

void Manager::loadProjectWorkspace()
{
    if (!gui)
        return;

    // Loading an ImGui layout must be deferred to the start of the next frame
    // (mirrors MainMenu's workspace Load action). The menu action that opens a
    // project runs mid-frame, after ImGui has already begun building the UI.
    fs::path workspacePath = projectPath / savedWorkspaceFileName;
    if (fs::exists(workspacePath))
    {
        layoutFileName = workspacePath.string();
        isReloadLayout = true;
        isReloadDefaultLayout = false;
    }
    else
    {
        isReloadDefaultLayout = true;
        isReloadLayout = false;
    }
}

// ---------------------------------------------------------------------------
// Open graph-editor file persistence (workspace.ini [OpenFiles] section)
// ---------------------------------------------------------------------------

void Manager::saveOpenEditorFiles(const fs::path &workspacePath)
{
    if (!projectOpened)
        return;

    // Collect the file loaded in each graph editor panel, but only when that
    // panel is currently visible (matching the user's "panel is opened" rule).
    std::string logicGraphFile, animatorFile, shaderGraphFile, animationFile, guiFile;
    if (showPanel.scriptEditor && panelLogicGraph)
        logicGraphFile = panelLogicGraph->getFilePath();
    if (showPanel.animatorEditor && panelAnimator)
        animatorFile = panelAnimator->getFilePath();
    if (showPanel.shaderEditor && panelShaderGraph)
        shaderGraphFile = panelShaderGraph->getFilePath();
    if (showPanel.animationEditor && panelAnimation)
        animationFile = panelAnimation->getFilePath();
    if (showPanel.guiEditor && panelGui)
        guiFile = panelGui->getFilePath();

    if (logicGraphFile.empty() && animatorFile.empty() &&
        shaderGraphFile.empty() && animationFile.empty() && guiFile.empty())
        return;

    // Store paths relative to the project so the workspace stays portable.
    auto relPath = [&](const std::string &p) -> std::string
    {
        if (p.empty())
            return "";
        std::error_code ec;
        fs::path abs = fs::absolute(p, ec);
        if (ec || abs.empty())
            return p;
        fs::path rel = fs::relative(abs, projectPath, ec);
        return ec ? abs.generic_string() : rel.generic_string();
    };

    std::ofstream f(workspacePath, std::ios::app);
    if (!f.is_open())
        return;

    f << "\n[OpenFiles]\n";
    if (!logicGraphFile.empty())  f << "LogicGraphFile="  << relPath(logicGraphFile)  << "\n";
    if (!animatorFile.empty())    f << "AnimatorFile="    << relPath(animatorFile)    << "\n";
    if (!shaderGraphFile.empty()) f << "ShaderGraphFile=" << relPath(shaderGraphFile) << "\n";
    if (!animationFile.empty())   f << "CinematicFile="   << relPath(animationFile)   << "\n";
    if (!guiFile.empty())         f << "GuiFile="         << relPath(guiFile)         << "\n";
    f << "\n";
    f.close();
}

void Manager::restoreOpenEditorFiles(const fs::path &workspacePath)
{
    if (!projectOpened)
        return;

    std::ifstream f(workspacePath);
    if (!f.is_open())
        return;

    std::string logicGraphFile, animatorFile, shaderGraphFile, animationFile, guiFile;
    bool inOpenFiles = false;
    std::string line;
    while (std::getline(f, line))
    {
        // Strip surrounding whitespace.
        size_t b = line.find_first_not_of(" \t\r");
        size_t e = line.find_last_not_of(" \t\r");
        if (b == std::string::npos)
            continue;
        line = line.substr(b, e - b + 1);

        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            inOpenFiles = (line == "[OpenFiles]");
            continue;
        }
        if (!inOpenFiles)
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (val.empty())
            continue;

        if      (key == "LogicGraphFile")  logicGraphFile  = val;
        else if (key == "AnimatorFile")    animatorFile    = val;
        else if (key == "ShaderGraphFile") shaderGraphFile = val;
        else if (key == "CinematicFile")   animationFile   = val;
        else if (key == "GuiFile")         guiFile         = val;
    }
    f.close();

    // Resolve stored (possibly relative) paths against the project directory.
    auto resolvePath = [&](const std::string &p) -> fs::path
    {
        fs::path cand(p);
        if (cand.is_absolute())
            return cand;
        return projectPath / p;
    };

    // Re-open each saved file through the same route a project-panel
    // double-click uses (shows the panel, loads the file, sets editor mode).
    if (panelProject && panelProject->onFileDoubleClicked)
    {
        if (!logicGraphFile.empty())
        {
            fs::path p = resolvePath(logicGraphFile);
            if (fs::exists(p))
                panelProject->onFileDoubleClicked(p.string());
        }
        if (!shaderGraphFile.empty())
        {
            fs::path p = resolvePath(shaderGraphFile);
            if (fs::exists(p))
                panelProject->onFileDoubleClicked(p.string());
        }
        if (!animatorFile.empty())
        {
            fs::path p = resolvePath(animatorFile);
            if (fs::exists(p))
                panelProject->onFileDoubleClicked(p.string());
        }
        if (!animationFile.empty())
        {
            fs::path p = resolvePath(animationFile);
            if (fs::exists(p))
                panelProject->onFileDoubleClicked(p.string());
        }
        if (!guiFile.empty())
        {
            fs::path p = resolvePath(guiFile);
            if (fs::exists(p))
                panelProject->onFileDoubleClicked(p.string());
        }
    }

    // Restoring the workspace re-opens editor files but must not steal focus
    // from the default layout, so drop any focus request the open calls queued.
    pendingFocusWindow.clear();
}

void Manager::loadDefaultWorldInto(kScene *target)
{
    if (!target || !world)
        return;

    HRSRC hRes = FindResource(NULL, "WORLD_DEFAULT", RT_RCDATA);
    if (!hRes)
    {
        std::cerr << "loadDefaultWorldInto: WORLD_DEFAULT resource not found\n";
        return;
    }
    HGLOBAL hData = LoadResource(NULL, hRes);
    DWORD size = SizeofResource(NULL, hRes);
    const char *bytes = static_cast<const char *>(LockResource(hData));
    if (!bytes || size == 0)
    {
        std::cerr << "loadDefaultWorldInto: WORLD_DEFAULT resource is empty\n";
        return;
    }

    json data;
    try
    {
        data = json::parse(bytes, bytes + size);
    }
    catch (const std::exception &e)
    {
        std::cerr << "loadDefaultWorldInto: parse error: " << e.what() << "\n";
        return;
    }

    if (!data.contains("scenes") || !data["scenes"].is_array() || data["scenes"].empty())
    {
        std::cerr << "loadDefaultWorldInto: no scenes in WORLD_DEFAULT\n";
        return;
    }

    // We only consume the first scene — the default world is intentionally
    // a single-scene placeholder; additional scenes would need their own
    // kScene allocation, which is outside this helper's contract.
    const auto &sceneJson = data["scenes"][0];

    if (sceneJson.contains("ambient_light"))
    {
        const auto &al = sceneJson["ambient_light"];
        target->setAmbientLightColor(kVec3(al.value("r", 0.1f),
                                           al.value("g", 0.1f),
                                           al.value("b", 0.1f)));
    }
    target->setShadowsEnabled(sceneJson.value("shadows_enabled", true));
    target->setShadowBias(sceneJson.value("shadow_bias", 0.0008f));
    target->setShadowNormalBias(sceneJson.value("shadow_normal_bias", 0.003f));
    target->setShadowMapResolution(sceneJson.value("shadow_map_resolution", 2048));
    target->setShadowSoftness(sceneJson.value("shadow_softness", 1.5f));
    target->setSkyboxAmbientEnabled(sceneJson.value("skybox_ambient_enabled", false));
    target->setSkyboxAmbientStrength(sceneJson.value("skybox_ambient_strength", 1.0f));

    if (sceneJson.contains("objects") && sceneJson["objects"].is_array())
    {
        kAssetManager *am = getAssetManager();
        for (const auto &objJson : sceneJson["objects"])
            loadObjectFromJson(objJson, target, world, am, projectPath, editorCamera);
    }
}

// ---------------------------------------------------------------------------
// Prefabs
// ---------------------------------------------------------------------------

// Forward declaration so prefab functions can push InstantiateCommands.
// Definition lives in the drag-and-drop helpers section further below.
static void pushInstantiateCommand(Manager *mgr, kObject *root);

bool Manager::saveSelectedAsPrefab(const kString &prefabName)
{
    if (!projectOpened || !selectedObject)
    {
        std::cerr << "saveSelectedAsPrefab: no project or no selection\n";
        return false;
    }

    // Serialize the selected object subtree (kObject::serialize already recurses
    // into children, so the full tree is captured).
    json rootJson = selectedObject->serialize();

    kPrefab prefab;
    prefab.setUuid(generateUuid());
    prefab.setName(prefabName);
    prefab.setRootJson(rootJson);

    // Place the .prefab into the currently-browsed project folder, picking a
    // unique filename if one already exists.
    fs::path dir = getCurrentDirPath();
    fs::path filePath = dir / (prefabName + ".prefab");
    int counter = 1;
    while (fs::exists(filePath))
    {
        filePath = dir / (prefabName + " " + std::to_string(counter) + ".prefab");
        counter++;
    }

    if (!prefab.saveToFile(filePath.string()))
        return false;

    checkAssetChange();
    if (panelProject != nullptr)
    {
        panelProject->triggerRefresh();
        panelProject->pendingSelectUuid = prefab.getUuid();
    }
    return true;
}

kObject *Manager::instantiatePrefabInScene(const fs::path &prefabPath)
{
    if (!projectOpened || !scene)
        return nullptr;

    kPrefab prefab;
    if (!prefab.loadFromFile(prefabPath.string()))
        return nullptr;

    // Generate fresh per-node UUIDs while preserving template UUIDs so the
    // editor can match instance nodes back to the prefab template later.
    json instanceJson = kPrefab::instantiateJson(prefab.getRootJson());

    kAssetManager *am = getAssetManager();
    kObject *root = loadObjectFromJson(instanceJson, scene, world, am,
                                       projectPath, editorCamera, nullptr);
    if (!root)
        return nullptr;

    // Tag the instance root with the source prefab UUID. Each instance node
    // already has its own template_uuid set by instantiateJson + the recursive
    // loader, but the prefab_ref only belongs on the root.
    root->setPrefabRef(prefab.getUuid());

    if (panelHierarchy)
        panelHierarchy->refreshList();

    selectedObject = root;
    selectObject(root->getUuid(), true);
    projectSaved = false;
    refreshWindowTitle();

    pushInstantiateCommand(this, root);
    return root;
}

void Manager::editPrefab(const fs::path &prefabPath)
{
    if (!projectOpened)
        return;

    // If we're already editing a different prefab, save & close it first.
    if (prefabEditing)
        closePrefabEditor(true);

    if (!editingPrefab.loadFromFile(prefabPath.string()))
    {
        std::cerr << "editPrefab: failed to load " << prefabPath << "\n";
        return;
    }

    editingPrefabPath = prefabPath;

    // --- Create the prefab's own isolated world with its own asset manager ---
    // The prefab editor uses a completely separate rendering pipeline — its own
    // kWorld, kScene, kCamera, and kRenderer — so it never touches the game
    // world's scene graph or FBO.
    if (!prefabAssetManager)
    {
        prefabAssetManager = createAssetManager();
    }
    if (!prefabWorld)
    {
        prefabWorld = createWorld(prefabAssetManager);
    }

    // Build an isolated scene for the prefab editor (visible in hierarchy).
    prefabScene = prefabWorld->createScene("Prefab");

    // Load the prefab subtree with template UUIDs preserved (no instantiateJson)
    // so saving round-trips cleanly back to the same node identities.
    //
    // IMPORTANT: VAOs are not shared between GL contexts, so we must switch to
    // the prefab driver before loading any meshes.  The meshes' GPU resources
    // will then live in the prefab driver's context where they can be rendered.
    kDriver *savedDriver = kDriver::getCurrent();
    if (prefabRenderer && prefabRenderer->getDriver())
    {
        prefabRenderer->getDriver()->makeCurrent(window);
        kDriver::setCurrent(prefabRenderer->getDriver());
    }

    kAssetManager *am = getAssetManager();
    prefabRoot = loadObjectFromJson(editingPrefab.getRootJson(),
                                    prefabScene, prefabWorld, am,
                                    projectPath, editorCamera, nullptr);

    // Apply skybox — its cube mesh VAO must also live in the prefab context.
    applyDefaultSkybox(prefabScene);

    // Add a default sun light to the prefab scene so objects are lit.
    // Name starts with '_' so the hierarchy panel hides it.
    {
        kLight *sun = prefabScene->addSunLight(
            kVec3(0.0f, 5.0f, 0.0f),
            kVec3(-0.5f, -1.0f, -0.5f),
            kVec3(1.0f, 1.0f, 1.0f),
            kVec3(1.0f, 1.0f, 1.0f));
        sun->setName("_SunLight_");
        sun->setPower(1.5f);
    }

    // Duplicate the editor grid scene for the prefab panel.
    if (!prefabEditorScene)
    {
        prefabEditorScene = prefabWorld->createScene("_EditorScene_");
        prefabEditorScene->setFrustumCullingEnabled(false);

        kMesh *gridMesh = kMeshGenerator::generatePlane();
        gridMesh->setPosition(kVec3(0.0f, -0.01f, 0.0f));
        prefabEditorScene->addMesh(gridMesh);

        kShader *gridShader = am->loadGlslFromResource("SHADER_GRID");
        kMaterial *gridMat = am->createMaterial(gridShader);
        gridMat->setTransparent(kTransparentType::TRANSP_TYPE_BLEND);
        gridMat->setSingleSided(false);
        gridMesh->setMaterial(gridMat);
    }

    // Restore the previously-current driver so the caller (ImGui / main loop)
    // continues to operate with the correct GL context.
    if (savedDriver)
    {
        savedDriver->makeCurrent(window);
        kDriver::setCurrent(savedDriver);
    }

    // Position an editor camera so the prefab is framed reasonably.
    prefabCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_FREE);
    prefabCamera->setPosition(kVec3(-5, 3, 8));
    prefabCamera->setLookAt(kVec3(0, 1, 0));
    prefabCamera->setFOV(60.0f);
    prefabCamera->setName("__PrefabEditCamera__");
    prefabWorld->addCamera(prefabCamera, prefabCamera->getUuid());

    prefabEditing = true;
    hierarchyShowsPrefab = true; // hierarchy now shows prefab scene graph

    // No auto-selection — the user can click to select objects as needed.
    prefabSelectedObjects.clear();
    prefabSelectedObject = nullptr;

    if (panelHierarchy)
        panelHierarchy->refreshList();
}

void Manager::closePrefabEditor(bool saveChanges)
{
    if (!prefabEditing)
        return;

    bool savedSuccessfully = false;
    kString savedPrefabUuid;

    if (saveChanges && prefabRoot)
    {
        // Serialize the (possibly-edited) root subtree back into the prefab JSON
        // and write the .prefab file. UUIDs are preserved because we never
        // re-randomized them on load.
        editingPrefab.setRootJson(prefabRoot->serialize());
        savedSuccessfully = editingPrefab.saveToFile(editingPrefabPath.string());
        savedPrefabUuid = editingPrefab.getUuid();
    }

    // Tear down the editor scene. Children are owned by the scene-graph nodes,
    // so deleting children of the root recursively frees the prefab subtree.
    if (prefabScene)
    {
        if (prefabScene->getRootNode())
        {
            for (kObject *child : prefabScene->getRootNode()->getChildren())
                deleteObjectRecursive(child);
        }
        if (prefabWorld)
            prefabWorld->removeScene(prefabScene);
        delete prefabScene;
        prefabScene = nullptr;
    }

    if (prefabCamera && prefabWorld)
    {
        prefabWorld->removeCamera(prefabCamera);
        delete prefabCamera;
        prefabCamera = nullptr;
    }

    // Clean up the editor grid scene (duplicated for prefab panel).
    if (prefabEditorScene && prefabWorld)
    {
        prefabWorld->removeScene(prefabEditorScene);
        delete prefabEditorScene;
        prefabEditorScene = nullptr;
    }

    // Destroy the prefab's isolated world and asset manager.
    if (prefabWorld)
    {
        delete prefabWorld;
        prefabWorld = nullptr;
    }
    if (prefabAssetManager)
    {
        delete prefabAssetManager;
        prefabAssetManager = nullptr;
    }

    prefabRoot = nullptr;
    prefabEditing = false;
    hierarchyShowsPrefab = false; // hierarchy returns to game world scene graph

    // Clear prefab-specific selection so nothing leaks to the world panel.
    prefabSelectedObjects.clear();
    prefabSelectedObject = nullptr;

    editingPrefabPath.clear();

    selectedObjects.clear();
    selectedObject = nullptr;

    // Propagate template edits to live instances. Each instance's world
    // transform (offset, rotation, scale) is preserved by
    // refreshAllPrefabInstances; per-instance subtree edits are dropped, since
    // those are only kept until the user clicks "Apply to Prefab".
    if (savedSuccessfully && !savedPrefabUuid.empty())
        refreshAllPrefabInstances(savedPrefabUuid);

    if (panelHierarchy)
        panelHierarchy->refreshList();
}

// ---------------------------------------------------------------------------
// Physics simulation
// ---------------------------------------------------------------------------
//
// On Play we walk the active scene, build a Jolt body per kObject that has
// a physics descriptor, and attach it via kObject::attachPhysics. The body's
// initial transform is taken from the object's CURRENT world transform, not
// the descriptor's stored position — the descriptor's position field is
// effectively unused after creation since the editor authors transforms via
// kObject::setPosition / setRotation directly.
//
// On Stop we destroy every body and detach. The kObject's local transform is
// restored separately by PanelGame's snapshot mechanism.

void Manager::startPhysicsSimulation()
{
    if (!scene)
        return;
    if (!physicsManager)
    {
        physicsManager = createPhysicsManager();
        if (!physicsManager)
        {
            std::cerr << "Physics: failed to create kPhysicsManager\n";
            return;
        }
    }
    // init() is idempotent — always run it so a previously created but
    // uninitialised manager can never silently swallow body/character creation.
    if (!physicsManager->init())
    {
        std::cerr << "Physics: failed to initialise kPhysicsManager\n";
        physicsManager = nullptr;
        return;
    }
    // Apply the project's named physics layers (same-layer-only collision).
    physicsManager->setLayerNames(layerSettings.layers);

    physicsBodies.clear();
    characterBodies.clear();

    std::function<void(kObject *)> walk = [&](kObject *node)
    {
        if (!node)
            return;
        if (node->getHasPhysicsDesc())
        {
            kPhysicsObjectDesc desc = node->getPhysicsDesc();
            // Seed the body from the object's current world transform so it
            // appears where the editor placed it, not at the descriptor's
            // (default-zero) position.
            desc.position = node->getGlobalPosition();
            desc.rotation = node->getGlobalRotation();
            kPhysicsObject *body = physicsManager->createObject(desc);
            if (body)
            {
                node->attachPhysics(body);
                physicsBodies.push_back(node);
            }
            else
            {
                std::cerr << "Physics: createObject failed for '" << node->getName() << "'\n";
            }
        }
        // A character controller is independent of the rigid-body descriptor.
        if (node->getHasCharacterDesc())
        {
            kCharacterControllerDesc cd = node->getCharacterDesc();
            cd.position = node->getGlobalPosition();
            cd.rotation = node->getGlobalRotation();
            kCharacterController *cc = physicsManager->createCharacter(cd);
            if (cc)
            {
                node->attachCharacter(cc);
                characterBodies.push_back(node);
            }
            else
            {
                std::cerr << "Physics: createCharacter failed for '" << node->getName() << "'\n";
            }
        }
        for (kObject *c : node->getChildren())
            walk(c);
    };
    walk(scene->getRootNode());
}

void Manager::stopPhysicsSimulation()
{
    if (!physicsManager)
        return;
    for (kObject *obj : physicsBodies)
    {
        if (kPhysicsObject *body = obj->getPhysicsObject())
        {
            obj->detachPhysics();
            physicsManager->destroyObject(body);
        }
    }
    physicsBodies.clear();

    for (kObject *obj : characterBodies)
    {
        if (kCharacterController *cc = obj->getCharacterController())
        {
            obj->detachCharacter();
            physicsManager->destroyCharacter(cc);
        }
    }
    characterBodies.clear();
}

void Manager::stepPhysics(float dt)
{
    if (!physicsManager || dt <= 0.0f)
        return;
    physicsManager->update(dt);
    // Sync each body's transform back into the scene-graph node so the
    // renderer picks up the new positions on the next draw.
    for (kObject *obj : physicsBodies)
        if (obj->getPhysicsObject())
            obj->syncFromPhysics();

    // Characters update their ground state inside physicsManager->update();
    // copy their resolved position back into the scene node.
    for (kObject *obj : characterBodies)
        if (obj->getCharacterController())
            obj->syncFromCharacter();

    // Fire OnCollision*/OnTrigger* events captured during the step to the
    // world's running script instances (kWorld::dispatchPhysicsContactEvents).
    if (world)
        world->dispatchPhysicsContactEvents(physicsManager, physicsBodies, characterBodies);
}

// ---------------------------------------------------------------------------
// Scripting
//
// Each attached AngelScript source is compiled to bytecode under
// Library/Scripts/<scriptUuid>.kbc. A script asset is identified per distinct
// source file, so two objects sharing one .as file share one compiled module
// asset. Compilation is checksum-gated: an unchanged source is not rebuilt.
// ---------------------------------------------------------------------------

void Manager::buildScripts(bool logSummary)
{
    if (!world || !scene)
        return;
    kScriptManager *sm = world->getScriptManager();
    if (!sm)
        return;

    fs::path scriptsDir = projectPath / "Library" / "Scripts";
    std::error_code ec;
    fs::create_directories(scriptsDir, ec);

    std::map<kString, kString> fileToAsset; // source path -> script-asset UUID
    std::map<kString, bool> fileCompiled;   // script-asset UUID -> compile OK
    bool changedMeta = false;
    int found = 0, okCount = 0, failCount = 0;

    std::function<void(kObject *)> walk = [&](kObject *node)
    {
        if (!node)
            return;
        for (kScript &comp : node->getScripts())
        {
            if (comp.fileName.empty())
                continue;
            ++found;
            if (!fs::exists(fs::path(comp.fileName)))
            {
                // ── Auto-generate missing .as from its source .logic ──────
                // When a .logic node graph is attached to an object, the
                // script fileName points to Library/GeneratedScripts/<uuid>.as,
                // but that file is only created when the .logic is opened and
                // saved in the Script Editor.  If it hasn't been saved yet,
                // compile it here so the user doesn't get a "source missing"
                // error just for picking a .logic from the script picker.
                fs::path genDir = projectPath / "Library" / "GeneratedScripts";
                fs::path asPath(comp.fileName);
                if (asPath.parent_path() == genDir)
                {
                    std::string uuid = asPath.stem().string();
                    fs::path assetsDir = projectPath / "Assets";
                    std::error_code ec2;
                    for (auto it = fs::recursive_directory_iterator(assetsDir, ec2);
                         it != fs::recursive_directory_iterator(); it.increment(ec2))
                    {
                        if (ec2)
                            break;
                        if (!it->is_regular_file() || it->path().extension() != ".logic")
                            continue;
                        try
                        {
                            std::ifstream f(it->path());
                            if (!f.is_open())
                                continue;
                            nlohmann::json j;
                            f >> j;
                            if (j.value("uuid", std::string()) == uuid)
                            {
                                kScriptGraph graph;
                                graph.fromJson(j);
                                kScriptGraphResult res = kScriptGraphCompiler::compile(graph);
                                if (res.success)
                                {
                                    fs::create_directories(genDir, ec2);
                                    std::ofstream out(asPath);
                                    if (out.is_open())
                                    {
                                        out << res.code;
                                        out.close();
                                    }
                                }
                                break;
                            }
                        }
                        catch (...) { }
                    }
                }

                if (!fs::exists(fs::path(comp.fileName)))
                {
                    ++failCount;
                    std::cerr << "buildScripts: source missing: " << comp.fileName << "\n";
                    if (panelConsole)
                        panelConsole->addLog(LogLevel::Error,
                                             "[Script] Source missing for '%s': %s",
                                             node->getName().c_str(), comp.fileName.c_str());
                    continue;
                }
            }

            // One script-asset UUID per distinct source file.
            auto fit = fileToAsset.find(comp.fileName);
            bool firstForFile = (fit == fileToAsset.end());
            kString sid = firstForFile
                              ? (comp.scriptUuid.empty() ? generateUuid() : comp.scriptUuid)
                              : fit->second;

            kString srcSum = generateFileChecksum(comp.fileName);
            fs::path bc = scriptsDir / (sid + ".kbc");

            if (firstForFile)
            {
                fileToAsset[comp.fileName] = sid;
                sm->registerScriptAsset(sid, comp.fileName);

                bool ok = true;
                if (comp.checksum != srcSum || !fs::exists(bc))
                {
                    ok = sm->compileToBytecode(sid, bc.string());
                    if (!ok)
                    {
                        std::cerr << "buildScripts: compile failed: " << comp.fileName << "\n";
                        if (panelConsole)
                            panelConsole->addLog(LogLevel::Error,
                                                 "[Script] Compile failed: %s", comp.fileName.c_str());
                    }
                }
                else
                {
                    sm->setBytecodePath(sid, bc.string());
                }
                fileCompiled[sid] = ok;
                if (ok)
                    ++okCount;
                else
                    ++failCount;
            }

            if (comp.scriptUuid != sid)
            {
                comp.scriptUuid = sid;
                changedMeta = true;
            }
            // Only record the checksum once the source actually compiled, so a
            // broken script is retried on the next build.
            if (fileCompiled[sid] && comp.checksum != srcSum)
            {
                comp.checksum = srcSum;
                changedMeta = true;
            }
        }
        for (kObject *c : node->getChildren())
            walk(c);
    };
    walk(scene->getRootNode());

    if (logSummary && panelConsole)
        panelConsole->addLog(found == 0 ? LogLevel::Warning : LogLevel::Info,
                             "[Script] Build: %d attached, %d compiled, %d failed.", found, okCount, failCount);

    if (changedMeta)
        projectSaved = false;
}

void Manager::applyInputBindings()
{
    if (!inputManager)
        return;

    // Rebuild digital-action bindings from the project's named input settings.
    // (Axes are bound in code by the runtime and are not authored here yet.)
    inputManager->clearActions();
    for (const auto &a : inputSettings.actions)
    {
        switch (a.device)
        {
        case INPUT_BINDING_MOUSE:
            inputManager->bindMouseButton(a.name, (unsigned int)a.binding);
            break;
        case INPUT_BINDING_GAMEPAD:
            inputManager->bindGamepadButton(a.name, (unsigned int)a.binding);
            break;
        case INPUT_BINDING_KEYBOARD:
        default:
            inputManager->bindKey(a.name, (unsigned int)a.binding);
            break;
        }
    }
}

void Manager::stepInput()
{
    if (inputManager)
        inputManager->update();
}

void Manager::startScripts()
{
    if (!world)
        return;
    loadInputSettings();            // refresh bindings authored in Project Settings
    buildScripts();                 // checksum-gated compile of attached scripts
    applyInputBindings();           // configure named input from project settings
    world->setInputManager(inputManager); // scripts resolve getAction()/getAxis() here

    // Route the runtime audio/physics managers to the script host so audio and
    // physics APIs resolve inside Awake()/Start()/Update()/etc.
    if (kScriptManager *sm = world->getScriptManager())
    {
        sm->setAudioManager(gameAudioManager);
        sm->setPhysicsManager(physicsManager);
    }

    world->startScripts();          // Awake() + Start() across the scene
}

void Manager::stopScripts()
{
    if (world)
        world->stopScripts();
}

void Manager::pollScriptChanges(float dt)
{
    // File-watch: while the editor is idle, periodically recompile any script
    // whose source changed on disk. buildScripts() is checksum-gated, so this
    // only does real work when a .as file was actually saved.
    if (!projectOpened || !world)
        return;
    scriptWatchTimer += dt;
    if (scriptWatchTimer < 1.5f)
        return;
    scriptWatchTimer = 0.0f;
    buildScripts();
}

// ---------------------------------------------------------------------------
// Drag-and-drop helpers
// ---------------------------------------------------------------------------

// Recursive tree walk to find any descendant by UUID. The cheap findObjectByUuid
// only scans direct children of the scene root; reparenting and prefab-aware
// operations need the full tree.
static kObject *findInTree(kObject *node, const kString &uuid)
{
    if (!node)
        return nullptr;
    if (node->getUuid() == uuid)
        return node;
    for (kObject *child : node->getChildren())
    {
        kObject *found = findInTree(child, uuid);
        if (found)
            return found;
    }
    return nullptr;
}

fs::path Manager::findAssetPathByUuid(const kString &assetUuid)
{
    auto it = fileMap.find(assetUuid);
    if (it == fileMap.end())
        return {};
    return projectPath / "Assets" / it->second.path;
}

// Push an InstantiateCommand for a freshly-spawned subtree rooted at `root`.
// All instantiate-* helpers funnel through this so the resulting commands
// match in shape (and undo behavior).
static void pushInstantiateCommand(Manager *mgr, kObject *root)
{
    if (!root || !mgr || !mgr->getScene())
        return;
    auto cmd = std::make_unique<InstantiateCommand>();
    cmd->manager = mgr;
    cmd->root = root;
    cmd->parent = root->getParent();
    cmd->scene = mgr->getScene();
    // nextSibling: the sibling immediately following `root` in parent's
    // children list, or nullptr if it's the last child. Used by undo→redo
    // round-trips to preserve original sibling order.
    if (cmd->parent)
    {
        auto kids = cmd->parent->getChildren();
        for (size_t i = 0; i < kids.size(); ++i)
            if (kids[i] == root && i + 1 < kids.size())
            {
                cmd->nextSibling = kids[i + 1];
                break;
            }
    }
    cmd->attached = true;
    cmd->ownsSubtree = false;
    mgr->undoRedo.push(std::move(cmd));
}

kObject *Manager::instantiateAssetFromUuid(const kString &assetUuid, const kVec3 &positionHint)
{
    if (!projectOpened || !scene)
        return nullptr;

    auto it = fileMap.find(assetUuid);
    if (it == fileMap.end())
        return nullptr;
    const FileInfo &info = it->second;
    fs::path assetPath = projectPath / "Assets" / info.path;
    kAssetManager *am = getAssetManager();

    if (info.type == "mesh")
    {
        // Imported mesh lives under Library/ImportedAssets/<uuid>.glb.
        fs::path glb = projectPath / "Library" / "ImportedAssets" / (assetUuid + ".glb");
        kMesh *mesh = nullptr;
        if (fs::exists(glb))
            mesh = am->loadMesh(glb.string());
        if (!mesh)
            mesh = new kMesh();
        mesh->setRefName(assetUuid); // remember source asset for save / re-import reload
        if (am)
            applyDefaultMaterial(mesh, am);
        assignImportChildUuidsRec(mesh);

        mesh->setName(fs::path(info.path).stem().string());
        scene->addMesh(mesh);
        mesh->setPosition(positionHint);
        kString uuid = mesh->getUuid();

        if (panelHierarchy)
            panelHierarchy->refreshList();
        selectedObject = mesh;
        selectObject(uuid, true);
        projectSaved = false;
        refreshWindowTitle();
        pushInstantiateCommand(this, mesh);
        return mesh;
    }
    else if (info.type == "prefab")
    {
        // instantiatePrefabInScene already pushes its own InstantiateCommand
        // (added below), so we just forward.
        kObject *root = instantiatePrefabInScene(assetPath);
        if (root)
            root->setPosition(positionHint);
        return root;
    }
    else if (info.type == "audio" || info.type == "particle")
    {
        // Spawn an empty object carrying the descriptor. The descriptor is
        // stored on the kObject; runtime spawns the actual source at game-start.
        kObject *obj = new kObject();
        obj->setName(fs::path(info.path).stem().string());
        scene->addObject(obj);
        obj->setPosition(positionHint);

        if (info.type == "audio")
        {
            obj->setType(kNodeType::NODE_TYPE_AUDIO);
            kAudioSource src;
            src.uuid = generateUuid();
            src.audioFile = assetUuid; // reference the audio asset by its project UUID
            obj->addAudioSource(src);
            if (kAssetManager *am = getAssetManager())
                applyAudioIcon(obj, am);
        }
        else
        {
            // .particle files only define parameters; the runtime kParticle
            // descriptor stores them inline. For now we create a default
            // emitter and tag its name with the source asset uuid so the
            // editor can later round-trip the params in/out of the file.
            kParticle p;
            p.uuid = generateUuid();
            p.name = fs::path(info.path).stem().string();
            obj->addParticle(p);
        }

        if (panelHierarchy)
            panelHierarchy->refreshList();
        selectedObject = obj;
        selectObject(obj->getUuid(), true);
        projectSaved = false;
        refreshWindowTitle();
        pushInstantiateCommand(this, obj);
        return obj;
    }

    else if (info.type == "ui")
    {
        // Ingame UI: a plain scene object that displays the referenced .ui
        // layout in the Game panel while the object is active. The asset link
        // is kept in the editor-side registry (see g_objectUiAssets) and is
        // written to the world file as an extra "ui" key on the object.
        kObject *obj = new kObject();
        obj->setName(fs::path(info.path).stem().string());
        scene->addObject(obj);
        obj->setPosition(positionHint);
        setObjectUiAsset(obj->getUuid(), assetUuid);

        // Parse (and cache) the layout now so invalid files are flagged early
        // and the Game panel picks it up on the very next frame.
        getGuiLayoutForAsset(assetUuid);

        if (panelHierarchy)
            panelHierarchy->refreshList();
        selectedObject = obj;
        selectObject(obj->getUuid(), true);
        projectSaved = false;
        refreshWindowTitle();
        pushInstantiateCommand(this, obj);
        return obj;
    }

    // Unknown / unsupported asset type — silently no-op.
    return nullptr;
}

// ---------------------------------------------------------------------------
// Ingame UI (.ui) object registry accessors
// ---------------------------------------------------------------------------

const std::unordered_map<kString, kString> &Manager::getObjectUiAssets() const
{
    return g_objectUiAssets;
}

void Manager::setObjectUiAsset(const kString &objectUuid, const kString &uiAssetUuid)
{
    if (objectUuid.empty())
        return;
    if (uiAssetUuid.empty())
        g_objectUiAssets.erase(objectUuid);
    else
        g_objectUiAssets[objectUuid] = uiAssetUuid;
}

kString Manager::getObjectUiAsset(const kString &objectUuid) const
{
    auto it = g_objectUiAssets.find(objectUuid);
    return (it == g_objectUiAssets.end()) ? kString("") : it->second;
}

void Manager::clearObjectUiAssets()
{
    g_objectUiAssets.clear();
    guiLayoutCache.clear();
    guiLayoutCacheMtime.clear();
}

std::shared_ptr<GuiLayout> Manager::getGuiLayoutForAsset(const kString &uiAssetUuid)
{
    if (uiAssetUuid.empty())
        return nullptr;

    fs::path path = findAssetPathByUuid(uiAssetUuid);
    if (path.empty() || !fs::exists(path))
    {
        guiLayoutCache.erase(uiAssetUuid);
        guiLayoutCacheMtime.erase(uiAssetUuid);
        return nullptr;
    }

    std::error_code ec;
    fs::file_time_type mtime = fs::last_write_time(path, ec);

    // Serve the cached parse while the file is unchanged.
    auto cached = guiLayoutCache.find(uiAssetUuid);
    if (cached != guiLayoutCache.end() && !ec)
    {
        auto mt = guiLayoutCacheMtime.find(uiAssetUuid);
        if (mt != guiLayoutCacheMtime.end() && mt->second == mtime)
            return cached->second;
    }

    auto layout = std::make_shared<GuiLayout>();
    if (!loadGuiLayoutFromFile(path.string(), *layout))
        return nullptr;

    guiLayoutCache[uiAssetUuid] = layout;
    if (!ec)
        guiLayoutCacheMtime[uiAssetUuid] = mtime;
    return layout;
}

// Returns the sibling immediately following `obj` in its parent's children
// list, or nullptr if `obj` is the last child or has no parent. Used to
// snapshot sibling order for reparent/reorder undo.
static kObject *nextSiblingOf(kObject *obj)
{
    if (!obj || !obj->getParent())
        return nullptr;
    auto kids = obj->getParent()->getChildren();
    for (size_t i = 0; i < kids.size(); ++i)
        if (kids[i] == obj && i + 1 < kids.size())
            return kids[i + 1];
    return nullptr;
}

// Insert `child` before `beforeSibling` in `parent`'s children list. Mirror
// of the helper in commands.cpp (kept duplicated to avoid leaking it through
// a public header).
static void insertBefore(kObject *parent, kObject *child, kObject *beforeSibling)
{
    if (!parent || !child)
        return;
    child->detachFromParent();
    if (!beforeSibling)
    {
        child->setParent(parent);
        return;
    }
    auto kids = parent->getChildren();
    std::vector<kObject *> tail;
    bool found = false;
    for (kObject *k : kids)
    {
        if (k == beforeSibling)
            found = true;
        if (found)
            tail.push_back(k);
    }
    for (kObject *k : tail)
        k->detachFromParent();
    child->setParent(parent);
    for (kObject *k : tail)
        k->setParent(parent);
}

void Manager::reparentObject(const kString &uuid, const kString &newParentUuid)
{
    if (!scene)
        return;
    kObject *obj = findInTree(scene->getRootNode(), uuid);
    if (!obj)
        return;

    kObject *newParent = newParentUuid.empty()
                             ? scene->getRootNode()
                             : findInTree(scene->getRootNode(), newParentUuid);
    if (!newParent)
        return;

    // Refuse the move if it would create a cycle (new parent is the obj or
    // a descendant of it).
    for (kObject *p = newParent; p != nullptr; p = p->getParent())
        if (p == obj)
            return;

    if (obj->getParent() == newParent)
        return;

    auto cmd = std::make_unique<ReparentCommand>();
    cmd->manager = this;
    cmd->objUuid = uuid;
    cmd->oldParent = obj->getParent();
    cmd->oldNextSibling = nextSiblingOf(obj);
    cmd->oldLocalPos = obj->getPosition();
    cmd->oldLocalRot = obj->getRotation();
    cmd->oldLocalScale = obj->getScale();

    // Reparent while preserving the object's world transform so the user
    // doesn't see the object jump when dragging it under a new parent.
    obj->setParentKeepTransform(newParent);

    cmd->newParent = newParent;
    cmd->newNextSibling = nextSiblingOf(obj);
    cmd->newLocalPos = obj->getPosition();
    cmd->newLocalRot = obj->getRotation();
    cmd->newLocalScale = obj->getScale();
    undoRedo.push(std::move(cmd));

    if (panelHierarchy)
        panelHierarchy->refreshList();
    projectSaved = false;
    refreshWindowTitle();
}

void Manager::reorderBefore(const kString &uuid, const kString &siblingUuid)
{
    if (!scene)
        return;
    kObject *obj = findInTree(scene->getRootNode(), uuid);
    kObject *sib = findInTree(scene->getRootNode(), siblingUuid);
    if (!obj || !sib)
        return;
    if (obj->getParent() != sib->getParent())
        return;

    kObject *parent = obj->getParent();
    if (!parent)
        return;

    auto cmd = std::make_unique<ReparentCommand>();
    cmd->manager = this;
    cmd->objUuid = uuid;
    cmd->oldParent = parent;
    cmd->oldNextSibling = nextSiblingOf(obj);
    cmd->oldLocalPos = obj->getPosition();

    insertBefore(parent, obj, sib);

    cmd->newParent = parent;
    cmd->newNextSibling = nextSiblingOf(obj);
    cmd->newLocalPos = obj->getPosition();
    undoRedo.push(std::move(cmd));

    if (panelHierarchy)
        panelHierarchy->refreshList();
    projectSaved = false;
    refreshWindowTitle();
}

kObject *Manager::findPrefabInstanceRoot(kObject *obj)
{
    for (kObject *p = obj; p != nullptr; p = p->getParent())
        if (!p->getPrefabRef().empty())
            return p;
    return nullptr;
}

bool Manager::createPrefabFromSelection()
{
    if (!projectOpened || !scene)
        return false;
    if (selectedObjects.empty())
        return false;

    // Resolve the selected objects in their original list order. The "last
    // selected" is the one held in `selectedObject` (if it's in the list),
    // matching how the rest of the editor treats the active selection.
    std::vector<kObject *> objs;
    for (const kString &uuid : selectedObjects)
    {
        kObject *o = findInTree(scene->getRootNode(), uuid);
        if (o)
            objs.push_back(o);
    }
    if (objs.empty())
        return false;

    kObject *lastSel = selectedObject ? selectedObject : objs.back();

    auto cmd = std::make_unique<CreatePrefabCommand>();
    cmd->manager = this;

    kObject *root = nullptr;
    bool createdEmpty = false;

    if (objs.size() == 1)
    {
        // Single selection — the object itself becomes the prefab root.
        root = objs[0];
    }
    else
    {
        // Multi-select: create an empty parent at the last-selected's position,
        // reparent every selected top-level object under it, save that as the
        // prefab.
        kObject *empty = new kObject();
        empty->setName(lastSel->getName().empty()
                           ? kString("PrefabGroup")
                           : (lastSel->getName() + "_Group"));
        scene->addObject(empty);
        empty->setPosition(lastSel->getPosition());

        cmd->createdEmpty = empty;
        cmd->createdEmptyParent = scene->getRootNode();
        cmd->createdEmptyPos = empty->getPosition();

        for (kObject *o : objs)
        {
            // Snapshot pre-move state so undo can restore parent + sibling +
            // full local transform exactly.
            CreatePrefabCommand::ChildMove m;
            m.objUuid = o->getUuid();
            m.oldParent = o->getParent();
            m.oldNextSibling = nextSiblingOf(o);
            m.oldLocalPos = o->getPosition();
            m.oldLocalRot = o->getRotation();
            m.oldLocalScale = o->getScale();

            // Reparent under the wrapper while preserving the world transform so
            // the grouped objects don't visually shift.
            o->setParentKeepTransform(empty);

            m.newLocalPos = o->getPosition();
            m.newLocalRot = o->getRotation();
            m.newLocalScale = o->getScale();
            cmd->childMoves.push_back(m);
        }
        root = empty;
        createdEmpty = true;
    }

    kString prefabName = root->getName().empty() ? kString("New Prefab") : root->getName();

    // Build the template JSON from the in-scene subtree, then walk it to
    // assign fresh template UUIDs while remembering the matching scene UUIDs
    // so we can stamp template_uuid back onto the in-scene objects.
    json templateJson = root->serialize();

    std::unordered_map<kString, kString> sceneToTemplate;
    std::function<void(json &)> assignTemplateUuids = [&](json &node)
    {
        kString sceneUuid = node.value("uuid", kString(""));
        kString tplUuid = generateUuid();
        node["uuid"] = tplUuid;
        // Strip prefab linkage from the template — only instances carry it.
        node.erase("prefab_ref");
        node.erase("template_uuid");
        if (!sceneUuid.empty())
            sceneToTemplate[sceneUuid] = tplUuid;
        if (node.contains("children") && node["children"].is_array())
            for (auto &c : node["children"])
                assignTemplateUuids(c);
    };
    assignTemplateUuids(templateJson);

    // Write the .prefab into the currently-browsed project folder, picking a
    // unique filename if one already exists.
    fs::path dir = getCurrentDirPath();
    fs::path filePath = dir / (prefabName + ".prefab");
    int counter = 1;
    while (fs::exists(filePath))
    {
        filePath = dir / (prefabName + " " + std::to_string(counter) + ".prefab");
        counter++;
    }

    kPrefab prefab;
    prefab.setUuid(generateUuid());
    prefab.setName(prefabName);
    prefab.setRootJson(templateJson);
    if (!prefab.saveToFile(filePath.string()))
    {
        // If we created a wrapper empty for multi-select but failed to save,
        // tear it down so the scene isn't left in a half-baked state.
        if (createdEmpty && root)
            scene->removeObject(root);
        return false;
    }

    // Stamp every in-scene node with the matching template_uuid so the subtree
    // becomes a live instance of the prefab we just created. Capture before/
    // after stamps so the command can revert them on undo.
    std::function<void(kObject *)> stampInstance = [&](kObject *node)
    {
        auto it = sceneToTemplate.find(node->getUuid());
        if (it != sceneToTemplate.end())
        {
            CreatePrefabCommand::StampChange s;
            s.objUuid = node->getUuid();
            s.oldPrefabRef = node->getPrefabRef();
            s.oldTemplateUuid = node->getTemplateUuid();
            s.newPrefabRef = ""; // root gets prefab_ref below; descendants stay empty
            s.newTemplateUuid = it->second;
            cmd->stamps.push_back(s);

            node->setTemplateUuid(it->second);
        }
        for (kObject *c : node->getChildren())
            stampInstance(c);
    };
    stampInstance(root);

    // The prefab_ref belongs only on the root. Patch the corresponding stamp
    // entry so the command reproduces this on redo.
    {
        CreatePrefabCommand::StampChange s;
        s.objUuid = root->getUuid();
        s.oldPrefabRef = root->getPrefabRef();
        s.oldTemplateUuid = root->getTemplateUuid(); // already set above
        s.newPrefabRef = prefab.getUuid();
        s.newTemplateUuid = root->getTemplateUuid();
        cmd->stamps.push_back(s);
        root->setPrefabRef(prefab.getUuid());
    }

    selectedObject = root;
    selectObject(root->getUuid(), true);

    if (panelHierarchy)
        panelHierarchy->refreshList();
    checkAssetChange();
    if (panelProject)
    {
        panelProject->triggerRefresh();
        panelProject->pendingSelectUuid = prefab.getUuid();
    }
    projectSaved = false;
    refreshWindowTitle();

    undoRedo.push(std::move(cmd));
    return true;
}

bool Manager::createPrefabFromObject(const kString& objectUuid, const fs::path& targetDir)
{
    if (!projectOpened || !world || objectUuid.empty())
        return false;

    // Search ALL scenes for the object — it may be in scene[1+].
    kObject *obj = nullptr;
    for (kScene *s : world->getScenes())
    {
        obj = findInTree(s->getRootNode(), objectUuid);
        if (obj)
            break;
    }
    if (!obj)
        return false;

    auto cmd = std::make_unique<CreatePrefabCommand>();
    cmd->manager = this;

    kString prefabName = obj->getName().empty() ? kString("New Prefab") : obj->getName();

    // Build the template JSON from the in-scene subtree.
    json templateJson = obj->serialize();

    std::unordered_map<kString, kString> sceneToTemplate;
    std::function<void(json &)> assignTemplateUuids = [&](json &node)
    {
        kString sceneUuid = node.value("uuid", kString(""));
        kString tplUuid = generateUuid();
        node["uuid"] = tplUuid;
        // Strip prefab linkage from the template — only instances carry it.
        node.erase("prefab_ref");
        node.erase("template_uuid");
        if (!sceneUuid.empty())
            sceneToTemplate[sceneUuid] = tplUuid;
        if (node.contains("children") && node["children"].is_array())
            for (auto &c : node["children"])
                assignTemplateUuids(c);
    };
    assignTemplateUuids(templateJson);

    // Write the .prefab into the target directory.
    fs::path filePath = targetDir / (prefabName + ".prefab");
    int counter = 1;
    while (fs::exists(filePath))
    {
        filePath = targetDir / (prefabName + " " + std::to_string(counter) + ".prefab");
        counter++;
    }

    kPrefab prefab;
    prefab.setUuid(generateUuid());
    prefab.setName(prefabName);
    prefab.setRootJson(templateJson);
    if (!prefab.saveToFile(filePath.string()))
        return false;

    // Stamp every in-scene node with template_uuid and set prefab_ref on root.
    // Record old values first so the undo command can restore them.
    std::function<void(kObject *)> stampInstance = [&](kObject *node)
    {
        auto it = sceneToTemplate.find(node->getUuid());
        if (it != sceneToTemplate.end())
        {
            CreatePrefabCommand::StampChange s;
            s.objUuid = node->getUuid();
            s.oldPrefabRef = node->getPrefabRef();
            s.oldTemplateUuid = node->getTemplateUuid();
            s.newPrefabRef = ""; // only root gets prefab_ref below
            s.newTemplateUuid = it->second;
            cmd->stamps.push_back(s);

            node->setTemplateUuid(it->second);
        }
        for (kObject *c : node->getChildren())
            stampInstance(c);
    };
    stampInstance(obj);

    // The prefab_ref belongs only on the root. Patch the corresponding stamp
    // entry so the command reproduces this on redo.
    {
        CreatePrefabCommand::StampChange s;
        s.objUuid = obj->getUuid();
        s.oldPrefabRef = obj->getPrefabRef();
        s.oldTemplateUuid = obj->getTemplateUuid(); // already set above
        s.newPrefabRef = prefab.getUuid();
        s.newTemplateUuid = obj->getTemplateUuid();
        cmd->stamps.push_back(s);
        obj->setPrefabRef(prefab.getUuid());
    }

    selectedObject = obj;
    selectObject(obj->getUuid(), true);

    if (panelHierarchy)
        panelHierarchy->refreshList();
    checkAssetChange();
    if (panelProject)
    {
        panelProject->triggerRefresh();
        panelProject->pendingSelectUuid = prefab.getUuid();
    }
    projectSaved = false;
    refreshWindowTitle();

    undoRedo.push(std::move(cmd));
    return true;
}

// ---------------------------------------------------------------------------
// Preview world management
// ---------------------------------------------------------------------------

void Manager::setEditorMode(EditorMode mode, const kString &assetPath, const kString &assetType)
{
    // Switching to GameWorld — no preview world needed.
    if (mode == EditorMode::GameWorld)
    {
        activeMode = EditorMode::GameWorld;
        previewAssetPath.clear();
        previewAssetType.clear();
        return;
    }

    // Switching to a preview mode — ensure the preview world exists.
    if (!previewWorld)
        initPreviewWorld();
    if (!previewWorld || !previewCamera)
        return;

    kScene *previewScene = previewWorld->getScenes().empty()
                               ? nullptr
                               : previewWorld->getScenes()[0];
    if (!previewScene)
        return;

    // Clear previous preview contents.
    {
        auto objs = previewScene->getObjects(); // copy — removeObject mutates
        for (kObject *o : objs)
            previewScene->removeObject(o);
    }

    activeMode = mode;
    previewAssetPath = assetPath;
    previewAssetType = assetType;

    // Reset camera to default 3/4 angle — framePreviewCamera() will reposition.
    previewCamPitch = 24.09f;
    previewCamYaw = 26.57f;
    previewCamDist = 5.0f;
    previewCamPivot = kVec3(0.0f);
    previewCamDragging = false;

    // Load the asset into the preview scene based on type.
    if (assetType == ".prefab" && !assetPath.empty())
    {
        kPrefab prefab;
        if (prefab.loadFromFile(assetPath))
        {
            // Spawn the prefab's root hierarchy into the preview scene.
            std::function<kObject *(const json &, kObject *)> spawnNode =
                [&](const json &j, kObject *parent) -> kObject *
            {
                if (!j.is_object())
                    return nullptr;
                kObject *obj = new kObject();
                obj->setName(j.value("name", kString("Object")));
                kVec3 pos(j.value("px", 0.0f), j.value("py", 0.0f), j.value("pz", 0.0f));
                obj->setPosition(pos);
                // Apply a default material so the preview renders visibly.
                if (parent)
                    obj->setParent(parent);
                else
                    previewScene->addObject(obj);
                if (j.contains("children") && j["children"].is_array())
                    for (auto &c : j["children"])
                        spawnNode(c, obj);
                return obj;
            };
            spawnNode(prefab.getRootJson(), nullptr);
        }
    }
    else if (assetType == ".particle" && !assetPath.empty())
    {
        // For particles, create a placeholder object — the particle panel
        // handles the actual preview. We just show an empty object at origin
        // with a descriptive name so the user sees something in the viewport.
        kObject *placeholder = new kObject();
        placeholder->setName(fs::path(assetPath).stem().string());
        previewScene->addObject(placeholder);
    }
    else if (assetType == ".animator" && !assetPath.empty())
    {
        // Placeholder for animator preview.
        kObject *placeholder = new kObject();
        placeholder->setName(fs::path(assetPath).stem().string());
        previewScene->addObject(placeholder);
    }

    // Frame the camera to the loaded content.
    framePreviewCamera();
}

void Manager::initPreviewWorld()
{
    if (previewWorld)
        return;

    previewWorld = createWorld(createAssetManager());
    kScene *previewScene = previewWorld->createScene("Preview");

    // Skybox — reuse the same default skybox as the editor scene.
    applyDefaultSkybox(previewScene);

    // Grid — add a flat grid object (simple quad or use the same grid as editor).
    // For simplicity, we rely on the renderer's built-in editor grid; the
    // preview world just needs a basic ground reference. A large flat plane
    // mesh centered at origin serves this purpose.
    kMesh *grid = kMeshGenerator::generatePlane(50.0f);
    grid->setName("Grid");
    previewScene->addMesh(grid);

    // Default sun light
    kLight *sun = previewScene->addSunLight(
        kVec3(0.0f, 5.0f, 0.0f),
        kVec3(-0.5f, -1.0f, -0.5f),
        kVec3(1.0f, 1.0f, 1.0f),
        kVec3(1.0f, 1.0f, 1.0f));
    sun->setPower(1.5f);

    previewScene->setAmbientLightColor(kVec3(0.15f, 0.15f, 0.15f));
    previewScene->setShadowsEnabled(true);

    // Preview camera — orbit-style, positioned by framePreviewCamera().
    previewCamera = new kCamera(nullptr, kCameraType::CAMERA_TYPE_LOCKED);
    previewCamera->setFOV(45.0f);
    previewCamera->setNearClip(0.1f);
    previewCamera->setFarClip(1000.0f);
}

void Manager::destroyPreviewWorld()
{
    delete previewCamera;
    previewCamera = nullptr;
    delete previewWorld;
    previewWorld = nullptr;
    previewAssetPath.clear();
    previewAssetType.clear();
}

void Manager::framePreviewCamera()
{
    if (!previewWorld || !previewCamera)
        return;

    kScene *previewScene = previewWorld->getScenes().empty()
                               ? nullptr
                               : previewWorld->getScenes()[0];
    if (!previewScene)
        return;

    // Compute combined AABB of all objects in the preview scene.
    kAABB combined;
    const auto &objects = previewScene->getObjects();
    for (kObject *obj : objects)
    {
        kMesh *mesh = dynamic_cast<kMesh *>(obj);
        if (mesh)
        {
            kAABB b = mesh->getWorldAABB();
            if (b.isValid())
            {
                combined.expandBy(b.min);
                combined.expandBy(b.max);
            }
        }
        else
        {
            // For non-mesh objects, expand around their world position.
            kVec3 p = obj->getGlobalPosition();
            combined.expandBy(p);
        }
    }

    if (combined.isValid())
    {
        previewCamPivot = combined.center();
        kVec3 he = combined.halfExtents();
        float radius = glm::length(he);
        if (radius < 0.001f)
            radius = 1.0f;
        previewCamDist = (radius / glm::tan(glm::radians(45.0f * 0.5f))) * 1.3f;
        if (previewCamDist < 1.0f)
            previewCamDist = 1.0f;
    }
    else
    {
        previewCamPivot = kVec3(0.0f);
        previewCamDist = 5.0f;
    }

    // Apply the orbit position.
    float pr = glm::radians(previewCamPitch);
    float yr = glm::radians(previewCamYaw);
    kVec3 camDir(std::cos(pr) * std::sin(yr),
                 std::sin(pr),
                 std::cos(pr) * std::cos(yr));
    previewCamera->setPosition(previewCamPivot + camDir * previewCamDist);
    previewCamera->setLookAt(previewCamPivot);
}

bool Manager::applyPrefabInstance(kObject *instanceRoot)
{
    if (!instanceRoot || instanceRoot->getPrefabRef().empty())
        return false;

    fs::path prefabPath;
    {
        kString refUuid = instanceRoot->getPrefabRef();
        for (const auto &p : fs::recursive_directory_iterator(projectPath / "Assets"))
        {
            if (!p.is_regular_file())
                continue;
            if (p.path().extension() != ".prefab")
                continue;
            kPrefab tmp;
            if (tmp.loadFromFile(p.path().string()) && tmp.getUuid() == refUuid)
            {
                prefabPath = p.path();
                break;
            }
        }
    }
    if (prefabPath.empty())
        return false;

    kPrefab prefab;
    if (!prefab.loadFromFile(prefabPath.string()))
        return false;

    // Serialize the in-scene subtree, then convert it into a template by
    // rewriting every node's UUID. Existing template_uuid values are preserved
    // so old-template nodes keep their identity; new nodes added in-instance
    // get fresh template UUIDs assigned now.
    json instanceJson = instanceRoot->serialize();

    std::function<void(json &)> toTemplate = [&](json &node)
    {
        kString existingTpl = node.value("template_uuid", kString(""));
        // Preserve the existing template UUID when available; otherwise
        // fall back to the current uuid so the template root identity
        // survives across repeated Apply operations.
        kString newUuid = existingTpl.empty()
                              ? node.value("uuid", generateUuid())
                              : existingTpl;
        node["uuid"] = newUuid;
        node.erase("prefab_ref");
        node.erase("template_uuid");
        if (node.contains("children") && node["children"].is_array())
            for (auto &c : node["children"])
                toTemplate(c);
    };
    toTemplate(instanceJson);

    prefab.setRootJson(instanceJson);
    if (!prefab.saveToFile(prefabPath.string()))
        return false;

    // Re-stamp the in-scene subtree with template_uuids that match the freshly
    // written template, so further "Apply" calls don't keep churning UUIDs.
    std::function<void(json &, kObject *)> stamp = [&](json &node, kObject *obj)
    {
        if (!obj)
            return;
        obj->setTemplateUuid(node.value("uuid", kString("")));
        if (!node.contains("children") || !node["children"].is_array())
            return;
        auto kids = obj->getChildren();
        for (size_t i = 0; i < kids.size() && i < node["children"].size(); ++i)
            stamp(node["children"][i], kids[i]);
    };
    stamp(instanceJson, instanceRoot);

    return true;
}

void Manager::refreshAllPrefabInstances(const kString &prefabUuid)
{
    if (!world || prefabUuid.empty())
        return;

    // Locate the .prefab file by UUID and load the (latest) template.
    fs::path prefabPath;
    for (const auto &p : fs::recursive_directory_iterator(projectPath / "Assets"))
    {
        if (!p.is_regular_file())
            continue;
        if (p.path().extension() != ".prefab")
            continue;
        kPrefab tmp;
        if (tmp.loadFromFile(p.path().string()) && tmp.getUuid() == prefabUuid)
        {
            prefabPath = p.path();
            break;
        }
    }
    if (prefabPath.empty())
        return;

    kPrefab prefab;
    if (!prefab.loadFromFile(prefabPath.string()))
        return;

    // ---------------------------------------------------------------------
    // Phase 1 — snapshot every live instance of this prefab.
    //
    // We capture enough state per instance to recreate it identically once
    // the old subtree has been destroyed: the original root UUID, world
    // transform, parent + sibling placement, name/active flags, the full
    // template_uuid → scene_uuid map for every node in the subtree (so
    // descendants keep their scene UUIDs across refresh), plus selection
    // info covering both the root and any descendant.
    // ---------------------------------------------------------------------
    struct InstanceState
    {
        kScene *scene = nullptr;
        kObject *root = nullptr; // valid only during snapshot
        kString rootUuid;
        kObject *parent = nullptr;
        kObject *nextSibling = nullptr;
        kVec3 pos = kVec3(0);
        kQuat rot = kQuat();
        kVec3 scl = kVec3(1);
        kString name;
        bool active = true;
        bool wasInSelectedList = false;
        bool wasSelectedObject = false; // root was the active selection
        kString selectedDescendantUuid; // selectedObject was a descendant; empty otherwise

        // template_uuid → old scene_uuid for every node in the subtree, root
        // included. Used to re-stamp UUIDs on the rebuilt instance JSON so
        // existing references (selectedObjects, scripts, etc.) keep working.
        std::unordered_map<kString, kString> templateToScene;
    };
    std::vector<InstanceState> instances;

    std::function<void(kScene *, kObject *)> collect = [&](kScene *s, kObject *node)
    {
        if (!node->getPrefabRef().empty() && node->getPrefabRef() == prefabUuid)
        {
            InstanceState st;
            st.scene = s;
            st.root = node;
            st.rootUuid = node->getUuid();
            st.parent = node->getParent();
            st.nextSibling = nextSiblingOf(node);
            st.pos = node->getPosition();
            st.rot = node->getRotation();
            st.scl = node->getScale();
            st.name = node->getName();
            st.active = node->getActive();
            st.wasInSelectedList = std::find(selectedObjects.begin(),
                                             selectedObjects.end(),
                                             st.rootUuid) != selectedObjects.end();
            st.wasSelectedObject = (selectedObject == node);

            // Walk the whole subtree to (a) record template→scene mappings
            // and (b) detect if the active selection points at any descendant.
            std::function<void(kObject *)> walk = [&](kObject *n)
            {
                if (!n->getTemplateUuid().empty())
                    st.templateToScene[n->getTemplateUuid()] = n->getUuid();
                if (selectedObject == n && !st.wasSelectedObject)
                    st.selectedDescendantUuid = n->getUuid();
                for (kObject *c : n->getChildren())
                    walk(c);
            };
            walk(node);

            instances.push_back(st);
            return; // don't descend into the prefab's own subtree
        }
        for (kObject *c : node->getChildren())
            collect(s, c);
    };

    for (kScene *s : world->getScenes())
        if (s && s->getRootNode())
            collect(s, s->getRootNode());

    if (instances.empty())
        return;

    kAssetManager *am = getAssetManager();
    kScene *savedScene = scene;

    // Selection pointer may dangle once we delete subtrees; clear it now and
    // re-point at the new root or new descendant in Phase 3.
    bool anySelectedRefreshed = false;
    for (const auto &i : instances)
        if (i.wasSelectedObject || !i.selectedDescendantUuid.empty())
            anySelectedRefreshed = true;
    if (anySelectedRefreshed)
        selectedObject = nullptr;

    // ---------------------------------------------------------------------
    // Phase 2 — destroy each old subtree, build a fresh one from the
    // template, re-stamp the captured UUIDs, restore properties.
    // ---------------------------------------------------------------------
    for (auto &i : instances)
    {
        i.root->detachFromParent();
        deleteObjectRecursive(i.root);
        i.root = nullptr;

        // Fresh JSON: instantiateJson writes template_uuid + a fresh scene
        // uuid on every node. We then walk that JSON and, for each node
        // whose template_uuid is in our map, override its uuid with the
        // captured scene uuid. Nodes that weren't in the old instance
        // (template additions) keep their fresh uuids.
        json instanceJson = kPrefab::instantiateJson(prefab.getRootJson());

        std::function<void(json &)> applyUuidMap = [&](json &n)
        {
            if (n.contains("template_uuid") && n["template_uuid"].is_string())
            {
                kString tplUuid = n["template_uuid"].get<std::string>();
                auto it = i.templateToScene.find(tplUuid);
                if (it != i.templateToScene.end())
                    n["uuid"] = it->second;
            }
            if (n.contains("children") && n["children"].is_array())
                for (auto &c : n["children"])
                    applyUuidMap(c);
        };
        applyUuidMap(instanceJson);

        // Defensive root override — covers the rare case where the old root
        // had no template_uuid (e.g. it was hand-built rather than spawned
        // by instantiatePrefabInScene).
        instanceJson["uuid"] = i.rootUuid;

        scene = i.scene;
        kObject *newRoot = loadObjectFromJson(instanceJson, i.scene, world,
                                              am, projectPath, editorCamera, nullptr);
        scene = savedScene;
        if (!newRoot)
            continue;

        // Re-stamp linkage and captured properties on the root.
        newRoot->setPrefabRef(prefabUuid);
        if (!i.name.empty())
            newRoot->setName(i.name);
        newRoot->setActive(i.active);
        // Use force-setters so the transform is always restored, even when
        // the object is marked as static (the static guard only blocks
        // runtime modifications — deserialization must always succeed).
        newRoot->setPositionForced(i.pos);
        newRoot->setRotationForced(i.rot);
        newRoot->setScaleForced(i.scl);

        // Restore parent + sibling placement.
        if (i.parent && i.parent != i.scene->getRootNode())
            insertBefore(i.parent, newRoot, i.nextSibling);
        else if (i.nextSibling)
            insertBefore(i.scene->getRootNode(), newRoot, i.nextSibling);

        // ---- Phase 3 — re-resolve selection pointer + list ----
        if (i.wasSelectedObject)
        {
            selectedObject = newRoot;
        }
        else if (!i.selectedDescendantUuid.empty())
        {
            // Descendant selection: walk the new subtree to find the node
            // that now carries the preserved scene UUID.
            std::function<kObject *(kObject *)> findUuid =
                [&](kObject *n) -> kObject *
            {
                if (!n)
                    return nullptr;
                if (n->getUuid() == i.selectedDescendantUuid)
                    return n;
                for (kObject *c : n->getChildren())
                    if (kObject *f = findUuid(c))
                        return f;
                return nullptr;
            };
            if (kObject *found = findUuid(newRoot))
                selectedObject = found;
        }

        // Defensive: the root UUID should still be in selectedObjects (we
        // never removed it), but re-add if it slipped out.
        if (i.wasInSelectedList &&
            std::find(selectedObjects.begin(), selectedObjects.end(),
                      i.rootUuid) == selectedObjects.end())
        {
            selectedObjects.push_back(i.rootUuid);
        }
    }

    if (panelHierarchy)
        panelHierarchy->refreshList();
    projectSaved = false;
    refreshWindowTitle();
}

void Manager::unpackPrefabInstance(kObject *instanceRoot)
{
    if (!instanceRoot)
        return;

    auto cmd = std::make_unique<UnpackPrefabCommand>();
    cmd->manager = this;

    std::function<void(kObject *)> walk = [&](kObject *node)
    {
        UnpackPrefabCommand::Entry e;
        e.objUuid = node->getUuid();
        e.prefabRef = node->getPrefabRef();
        e.templateUuid = node->getTemplateUuid();
        cmd->entries.push_back(e);

        node->setPrefabRef("");
        node->setTemplateUuid("");
        for (kObject *c : node->getChildren())
            walk(c);
    };
    walk(instanceRoot);

    if (panelHierarchy)
        panelHierarchy->refreshList();
    projectSaved = false;
    refreshWindowTitle();

    undoRedo.push(std::move(cmd));
}

// ---------------------------------------------------------------------------
// Prefab dirty checking and revert
// ---------------------------------------------------------------------------

// Helper: recursively compare two JSON nodes ignoring transform/linkage keys
// (uuid, position, rotation, scale, prefab_ref, template_uuid) that are
// expected to differ between a prefab template and a live instance.
// Also ignores camera-derived keys (look_at, up_axis) that are computed from
// position/rotation and therefore naturally diverge on prefab instances.
static bool jsonEqualsIgnoreTransform(const nlohmann::json &a, const nlohmann::json &b)
{
    if (a.type() != b.type())
    {
        // Treat null / missing vs empty array as equivalent — older prefab
        // templates may omit keys that newer serialization emits as [].
        if ((a.is_array() && a.empty() && (b.is_null() || b.type() == nlohmann::json::value_t::discarded)) ||
            (b.is_array() && b.empty() && (a.is_null() || a.type() == nlohmann::json::value_t::discarded)))
            return true;
        return false;
    }

    auto isIgnored = [](const std::string &key) -> bool {
        return key == "uuid" || key == "position" || key == "rotation" ||
               key == "scale" || key == "prefab_ref" || key == "template_uuid" ||
               key == "look_at" || key == "up_axis" ||
               key == "aspect_ratio" || key == "scene_uuid";
    };

    if (a.is_object())
    {
        // Collect all keys from both objects
        std::set<std::string> keys;
        for (auto it = a.begin(); it != a.end(); ++it)
        {
            if (isIgnored(it.key()))
                continue;
            keys.insert(it.key());
        }
        for (auto it = b.begin(); it != b.end(); ++it)
        {
            if (isIgnored(it.key()))
                continue;
            keys.insert(it.key());
        }
        for (const std::string &k : keys)
        {
            bool aHas = a.contains(k);
            bool bHas = b.contains(k);
            // Treat a missing key as equivalent to an empty array — older
            // prefab templates may omit keys (script, audio_sources,
            // audio_listeners, particle) that newer serialization emits as [].
            if (!aHas && bHas && b[k].is_array() && b[k].empty())
                continue;
            if (!bHas && aHas && a[k].is_array() && a[k].empty())
                continue;
            if (!aHas || !bHas)
                return false;
            if (!jsonEqualsIgnoreTransform(a[k], b[k]))
                return false;
        }
        return true;
    }
    else if (a.is_array())
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!jsonEqualsIgnoreTransform(a[i], b[i]))
                return false;
        return true;
    }
    else
    {
        return a == b;
    }
}

bool Manager::isPrefabDirty(kObject *instanceRoot)
{
    if (!instanceRoot || instanceRoot->getPrefabRef().empty())
        return false;

    kString prefabUuid = instanceRoot->getPrefabRef();

    // Load or retrieve the cached prefab template JSON.
    nlohmann::json templateRoot;
    {
        auto it = prefabTemplateCache.find(prefabUuid);
        if (it != prefabTemplateCache.end())
        {
            templateRoot = it->second;
        }
        else
        {
            // Locate and load the .prefab file by UUID.
            fs::path prefabPath;
            for (const auto &p : fs::recursive_directory_iterator(projectPath / "Assets"))
            {
                if (!p.is_regular_file())
                    continue;
                if (p.path().extension() != ".prefab")
                    continue;
                kPrefab tmp;
                if (tmp.loadFromFile(p.path().string()) && tmp.getUuid() == prefabUuid)
                {
                    prefabPath = p.path();
                    break;
                }
            }
            if (prefabPath.empty())
                return false;

            kPrefab prefab;
            if (!prefab.loadFromFile(prefabPath.string()))
                return false;

            templateRoot = prefab.getRootJson();
            prefabTemplateCache[prefabUuid] = templateRoot;
        }
    }

    // Serialize the current instance subtree.
    nlohmann::json instanceJson = instanceRoot->serialize();

    // Compare, ignoring transform keys.
    return !jsonEqualsIgnoreTransform(templateRoot, instanceJson);
}

void Manager::revertPrefabInstance(kObject *instanceRoot)
{
    if (!instanceRoot || instanceRoot->getPrefabRef().empty())
        return;

    kString prefabUuid = instanceRoot->getPrefabRef();
    kString rootUuid   = instanceRoot->getUuid();

    // Locate and load the .prefab file.
    fs::path prefabPath;
    for (const auto &p : fs::recursive_directory_iterator(projectPath / "Assets"))
    {
        if (!p.is_regular_file())
            continue;
        if (p.path().extension() != ".prefab")
            continue;
        kPrefab tmp;
        if (tmp.loadFromFile(p.path().string()) && tmp.getUuid() == prefabUuid)
        {
            prefabPath = p.path();
            break;
        }
    }
    if (prefabPath.empty())
        return;

    kPrefab prefab;
    if (!prefab.loadFromFile(prefabPath.string()))
        return;

    // Find which scene this instance lives in.
    kScene *instanceScene = nullptr;
    for (kScene *s : world->getScenes())
    {
        if (findInTree(s->getRootNode(), rootUuid))
        {
            instanceScene = s;
            break;
        }
    }
    if (!instanceScene)
        return;

    // --- Capture the BEFORE state (full serialized JSON snapshot) ---
    json beforeJson = instanceRoot->serialize();

    // Per-instance properties that survive the revert.
    kObject *parent      = instanceRoot->getParent();
    kObject *nextSibling = nextSiblingOf(instanceRoot);
    kVec3    pos         = instanceRoot->getPosition();
    kQuat    rot         = instanceRoot->getRotation();
    kVec3    scl         = instanceRoot->getScale();
    kString  name        = instanceRoot->getName();
    bool     active      = instanceRoot->getActive();

    // Selection state before revert.
    bool    wasSelected = (selectedObject == instanceRoot);
    bool    wasInList   = std::find(selectedObjects.begin(), selectedObjects.end(),
                                    rootUuid) != selectedObjects.end();
    kString selectedDescendantUuid;
    if (!wasSelected)
    {
        std::function<void(kObject *)> findSel = [&](kObject *n)
        {
            if (n == selectedObject)
                selectedDescendantUuid = n->getUuid();
            for (kObject *c : n->getChildren())
                findSel(c);
        };
        findSel(instanceRoot);
    }

    // --- Build the new (clean) subtree from the prefab template ---
    kScene *savedScene = scene;
    scene = instanceScene;

    instanceRoot->detachFromParent();
    deleteObjectRecursive(instanceRoot);

    // Build fresh JSON with new UUIDs, then re-stamp preserved scene UUIDs.
    json rebuiltJson = kPrefab::instantiateJson(prefab.getRootJson());

    // Recursive helper: find a scene UUID in the before-snapshot that matches
    // a given template_uuid. Uses std::function to allow recursion.
    std::function<kString(const json &, const kString &)> findSceneUuid;
    findSceneUuid = [&](const json &haystack, const kString &tplUuid) -> kString
    {
        if (!haystack.is_object()) return {};
        if (haystack.value("template_uuid", kString("")) == tplUuid)
            return haystack.value("uuid", kString(""));
        if (haystack.contains("children") && haystack["children"].is_array())
            for (const auto &c : haystack["children"])
                if (kString found = findSceneUuid(c, tplUuid); !found.empty())
                    return found;
        return {};
    };

    // Walk the rebuilt JSON and remap UUIDs to match the old scene UUIDs
    // wherever the template_uuid matches.
    std::function<void(json &)> remapUuids = [&](json &n)
    {
        if (n.contains("template_uuid") && n["template_uuid"].is_string())
        {
            kString sceneUuid = findSceneUuid(beforeJson, n["template_uuid"].get<kString>());
            if (!sceneUuid.empty())
                n["uuid"] = sceneUuid;
        }
        if (n.contains("children") && n["children"].is_array())
            for (auto &c : n["children"])
                remapUuids(c);
    };
    remapUuids(rebuiltJson);
    rebuiltJson["uuid"] = rootUuid; // preserve root UUID

    kAssetManager *am = getAssetManager();
    kObject *newRoot = loadObjectFromJson(rebuiltJson, instanceScene, world,
                                           am, projectPath, editorCamera, nullptr);
    scene = savedScene;
    if (!newRoot)
        return;

    // Apply per-instance properties that survive the revert.
    newRoot->setPrefabRef(prefabUuid);
    if (!name.empty())
        newRoot->setName(name);
    newRoot->setActive(active);
    // Use force-setters so the transform is always restored, even when
    // the object is marked as static.
    newRoot->setPositionForced(pos);
    newRoot->setRotationForced(rot);
    newRoot->setScaleForced(scl);

    // Restore parent + sibling placement.
    kObject *sceneRoot = instanceScene->getRootNode();
    if (parent && parent != sceneRoot)
        insertBefore(parent, newRoot, nextSibling);
    else if (nextSibling)
        insertBefore(sceneRoot, newRoot, nextSibling);

    // Restore selection.
    if (wasSelected)
        selectedObject = newRoot;
    else if (!selectedDescendantUuid.empty())
    {
        std::function<kObject *(kObject *)> findUuid = [&](kObject *n) -> kObject *
        {
            if (!n) return nullptr;
            if (n->getUuid() == selectedDescendantUuid) return n;
            for (kObject *c : n->getChildren())
                if (kObject *f = findUuid(c)) return f;
            return nullptr;
        };
        if (kObject *found = findUuid(newRoot))
            selectedObject = found;
    }
    if (wasInList && std::find(selectedObjects.begin(), selectedObjects.end(),
                                rootUuid) == selectedObjects.end())
        selectedObjects.push_back(rootUuid);

    // --- Capture the AFTER state ---
    json afterJson = newRoot->serialize();

    // --- Push an undoable command ---
    undoRedo.push(std::make_unique<PropertyCommand>(
        [this, beforeJson, rootUuid, instanceScene,
         parent, nextSibling, pos, rot, scl, name, active, prefabUuid,
         wasSelected, wasInList, selectedDescendantUuid]()
        {
            replacePrefabSubtree(rootUuid, beforeJson, instanceScene,
                                 parent, nextSibling,
                                 pos, rot, scl, name, active, prefabUuid,
                                 wasSelected, wasInList, selectedDescendantUuid);
        },
        [this, afterJson, rootUuid, instanceScene,
         parent, nextSibling, pos, rot, scl, name, active, prefabUuid,
         wasSelected, wasInList, selectedDescendantUuid]()
        {
            replacePrefabSubtree(rootUuid, afterJson, instanceScene,
                                 parent, nextSibling,
                                 pos, rot, scl, name, active, prefabUuid,
                                 wasSelected, wasInList, selectedDescendantUuid);
        }));

    projectSaved = false;
    refreshWindowTitle();
}

void Manager::replacePrefabSubtree(const kString &rootUuid,
                                   const nlohmann::json &stateJson,
                                   kScene *targetScene,
                                   kObject *parent, kObject *nextSibling,
                                   const kVec3 &pos, const kQuat &rot, const kVec3 &scl,
                                   const kString &name, bool active,
                                   const kString &prefabUuid,
                                   bool restoreSelection, bool restoreInList,
                                   const kString &selectedDescendantUuid)
{
    if (!targetScene || rootUuid.empty())
        return;

    // Find the current instance root in the scene graph.
    kObject *currentRoot = findInTree(targetScene->getRootNode(), rootUuid);
    if (!currentRoot)
        return;

    // Destroy the old subtree.
    kScene *savedScene = scene;
    scene = targetScene;

    currentRoot->detachFromParent();
    deleteObjectRecursive(currentRoot);

    // Rebuild from the JSON snapshot.
    kAssetManager *am = getAssetManager();
    kObject *newRoot = loadObjectFromJson(stateJson, targetScene, world,
                                           am, projectPath, editorCamera, nullptr);
    scene = savedScene;
    if (!newRoot)
        return;

    // Re-apply per-instance properties.
    newRoot->setPrefabRef(prefabUuid);
    if (!name.empty())
        newRoot->setName(name);
    newRoot->setActive(active);
    // Use force-setters so the transform is always restored, even when
    // the object is marked as static.
    newRoot->setPositionForced(pos);
    newRoot->setRotationForced(rot);
    newRoot->setScaleForced(scl);

    // Restore parent + sibling placement.
    kObject *sceneRoot = targetScene->getRootNode();
    if (parent && parent != sceneRoot)
        insertBefore(parent, newRoot, nextSibling);
    else if (nextSibling)
        insertBefore(sceneRoot, newRoot, nextSibling);

    // Restore selection.
    if (restoreSelection)
        selectedObject = newRoot;
    else if (!selectedDescendantUuid.empty())
    {
        std::function<kObject *(kObject *)> findUuid = [&](kObject *n) -> kObject *
        {
            if (!n) return nullptr;
            if (n->getUuid() == selectedDescendantUuid) return n;
            for (kObject *c : n->getChildren())
                if (kObject *f = findUuid(c)) return f;
            return nullptr;
        };
        if (kObject *found = findUuid(newRoot))
            selectedObject = found;
    }
    if (restoreInList && std::find(selectedObjects.begin(), selectedObjects.end(),
                                    rootUuid) == selectedObjects.end())
        selectedObjects.push_back(rootUuid);

    if (panelHierarchy)
        panelHierarchy->refreshList();
}

void Manager::clearPrefabTemplateCache()
{
    prefabTemplateCache.clear();
}

kTexture2D *Manager::getProjectTexture(const kString &textureUuid, const kString &uniformName)
{
    if (textureUuid.empty())
        return nullptr;

    // A texture used as a normal map must be sampled in linear space, no matter
    // its own color-space setting — sampling normal vectors through sRGB decode
    // tilts them and wrecks the lighting. Cache that variant under a distinct
    // key so the same image used as albedo (sRGB) and normal (linear) coexist.
    const bool forceLinear = (uniformName == "normalMap");
    kString cacheKey = forceLinear ? (textureUuid + "#linear") : textureUuid;

    auto cit = textureCache.find(cacheKey);
    if (cit != textureCache.end())
        return cit->second;

    kAssetManager *am = getAssetManager();
    if (!am)
        return nullptr;

    kTexture2D *tex = nullptr;
    auto fit = fileMap.find(textureUuid);
    if (fit != fileMap.end() && fit->second.type == "image")
    {
        // Per-texture import settings written by the Image Import Settings panel.
        bool sRGB = true;
        int wrapMode = 0;
        int filterMode = 1;
        fs::path metaPath = projectPath / "Library" / "Metadata" / (textureUuid + ".json");
        if (fs::exists(metaPath))
        {
            try
            {
                std::ifstream mf(metaPath);
                nlohmann::json mj;
                mf >> mj;
                // Channel: 0=sRGB, 1=Linear Color, 2=Linear Grayscale (migrate old bool).
                int channel = mj.value("channel", mj.value("sRGB", true) ? 0 : 1);
                sRGB = (channel == 0);
                wrapMode = mj.value("wrapMode", 0);
                filterMode = mj.value("filterMode", 1);
            }
            catch (...)
            {
            }
        }
        if (forceLinear)
            sRGB = false; // normal maps are always linear

        // Prefer the imported .dds in Library (resized/compressed per settings).
        // Fall back to the original image in Assets/ if it isn't imported yet
        // (loadTexture2D reads PNG/JPG via stb_image but not .dds).
        fs::path ddsPath = projectPath / "Library" / "ImportedAssets" / (textureUuid + ".dds");
        if (fs::exists(ddsPath))
            tex = am->loadTexture2DDDS(ddsPath.string(), uniformName, sRGB, wrapMode, filterMode);

        if (!tex)
        {
            fs::path imgPath = projectPath / "Assets" / fit->second.path;
            if (fs::exists(imgPath))
                tex = am->loadTexture2D(imgPath.string(), uniformName,
                                        sRGB ? kTextureFormat::TEX_FORMAT_SRGBA
                                             : kTextureFormat::TEX_FORMAT_RGBA);
        }
    }
    textureCache[cacheKey] = tex; // cache even if null, to avoid re-probing
    return tex;
}

bool Manager::reimportTexture(const kString &textureUuid)
{
    auto fit = fileMap.find(textureUuid);
    if (fit == fileMap.end() || fit->second.type != "image")
        return false;

    fs::path srcPath = projectPath / "Assets" / fit->second.path;
    if (!fs::exists(srcPath))
        return false;

    fs::path ddsPath = projectPath / "Library" / "ImportedAssets" / (textureUuid + ".dds");
    fs::path metaPath = projectPath / "Library" / "Metadata" / (textureUuid + ".json");

    // Map the inspector's import settings to converter options. (sRGB, wrap and
    // filter are load-time only, so they're not passed to the converter here —
    // getProjectTexture reads them straight from the .meta.)
    ImageImportOptions opt;
    if (fs::exists(metaPath))
    {
        try
        {
            std::ifstream mf(metaPath);
            nlohmann::json mj;
            mf >> mj;
            static const int kSizes[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};
            int sizeIdx = mj.value("maxSizeIndex", 7);
            sizeIdx = std::max(0, std::min(7, sizeIdx));
            opt.maxSize = kSizes[sizeIdx];
            opt.compression = mj.value("compression", 2);
            opt.alphaSource = mj.value("alphaSource", 0);
            // Channel: 0=sRGB, 1=Linear Color, 2=Linear Grayscale (migrate old bool).
            int channel = mj.value("channel", mj.value("sRGB", true) ? 0 : 1);
            opt.sRGB = (channel == 0);
            opt.grayscale = (channel == 2);
            opt.generateMipmap = mj.value("generateMipmap", true);
            opt.flipVertical = mj.value("flipVertical", false);

            // Normal map (imageType 3): linear data, with its own options.
            opt.isNormalMap = (mj.value("imageType", 0) == 3);
            opt.flipGreen = mj.value("flipGreen", false);
            opt.fromGrayscale = mj.value("fromGrayscale", false);
            opt.bumpiness = mj.value("bumpiness", 1.0f);
            opt.normalFilter = mj.value("normalFilter", 0);
        }
        catch (...)
        {
        }
    }

    std::error_code ec;
    fs::create_directories(ddsPath.parent_path(), ec);
    if (!convertImageToDDS(srcPath, ddsPath, opt))
    {
        if (panelConsole)
            panelConsole->addLog(LogLevel::Error, "[Import] Failed to reimport texture '%s'",
                                 srcPath.generic_string().c_str());
        return false;
    }

    // Drop the cached GPU texture(s) so the next material build reloads the new
    // .dds, then rebuild every object's material so the change shows live.
    // Both the default (sRGB) and the normal-map (linear) variants are evicted.
    textureCache.erase(textureUuid);
    textureCache.erase(textureUuid + "#linear");
    reapplyStoredMaterials();
    return true;
}

bool Manager::reimportMesh(const kString &meshUuid)
{
    auto fit = fileMap.find(meshUuid);
    if (fit == fileMap.end() || fit->second.type != "mesh")
        return false;

    fs::path srcPath = projectPath / "Assets" / fit->second.path;
    if (!fs::exists(srcPath))
        return false;

    fs::path glbPath = projectPath / "Library" / "ImportedAssets" / (meshUuid + ".glb");
    fs::path metaPath = projectPath / "Library" / "Metadata" / (meshUuid + ".json");
    fs::path thumbPath = projectPath / "Library" / "Thumbnails" / (meshUuid + ".png");

    MeshImportOptions opt;
    if (fs::exists(metaPath))
    {
        try
        {
            std::ifstream mf(metaPath);
            nlohmann::json mj;
            mf >> mj;
            opt.scaleFactor = mj.value("scaleFactor", 1.0f);
            opt.tangents = mj.value("tangents", 0);
            opt.importAnimation = mj.value("importAnimation", true);
        }
        catch (...)
        {
        }
    }

    std::error_code ec;
    fs::create_directories(glbPath.parent_path(), ec);
    std::string err;
    std::vector<std::string> warns;
    bool ok = convertMeshToGlbEx(srcPath, glbPath, opt, &err, &warns);
    if (panelConsole)
        for (const std::string &w : warns)
            panelConsole->addLog(LogLevel::Warning, "[Import] %s: %s",
                                 srcPath.generic_string().c_str(), w.c_str());
    if (!ok)
    {
        std::cerr << "reimportMesh failed for " << srcPath << ": " << err << "\n";
        if (panelConsole)
            panelConsole->addLog(LogLevel::Error, "[Import] Failed to reimport mesh '%s': %s",
                                 srcPath.generic_string().c_str(),
                                 err.empty() ? "unknown error" : err.c_str());
        return false;
    }

    // Regenerate the thumbnail from the new .glb (deleted so it isn't skipped).
    if (fs::exists(thumbPath))
        fs::remove(thumbPath, ec);
    thumbnailQueue.push_back({meshUuid, glbPath, thumbPath, "mesh"});

    // Queue a deferred reload so scene instances pick up the new geometry. Done
    // next frame (not here) to avoid tearing down objects mid panel-draw.
    if (std::find(pendingMeshReloads.begin(), pendingMeshReloads.end(), meshUuid) == pendingMeshReloads.end())
        pendingMeshReloads.push_back(meshUuid);

    // Keep the model selected through the refresh so the inspector doesn't blank out.
    if (panelProject)
    {
        panelProject->pendingSelectUuid = meshUuid;
        panelProject->triggerRefresh();
    }
    return true;
}

// Recursively walk a node subtree and collect kMesh objects whose refName
// matches a pending reload UUID. Returns {mesh, parent} pairs.
static void collectMeshMatches(kObject *node, const std::vector<kString> &pending,
                               std::vector<std::pair<kObject *, kObject *>> &out)
{
    kMesh *m = dynamic_cast<kMesh *>(node);
    if (m && !m->getRefName().empty() &&
        std::find(pending.begin(), pending.end(), m->getRefName()) != pending.end())
    {
        out.push_back({node, node->getParent()});
    }
    for (kObject *child : node->getChildren())
        collectMeshMatches(child, pending, out);
}

void Manager::processPendingMeshReloads()
{
    if (pendingMeshReloads.empty())
        return;

    // Process one world's scenes: find matching meshes, detach/delete/reload.
    auto processScenes = [&](kWorld *w, kAssetManager *am) -> bool {
        if (!w) return false;
        bool any = false;
        auto scenes = w->getScenes();
        // Skip index 0 for the main world (editor gizmo scene); the prefab
        // world has no editor scene so process all of its scenes.
        size_t start = (w == world) ? 1 : 0;
        for (size_t si = start; si < scenes.size(); ++si)
        {
            kScene *s = scenes[si];
            if (!s || !s->getRootNode()) continue;

            // Recursively collect all meshes in this scene whose asset UUID
            // is queued for reload (handles nested / prefab-instance meshes).
            std::vector<std::pair<kObject *, kObject *>> matches; // {mesh, parent}
            for (kObject *child : s->getRootNode()->getChildren())
                collectMeshMatches(child, pendingMeshReloads, matches);

            std::vector<nlohmann::json> snapshots;
            snapshots.reserve(matches.size());
            for (auto &pair : matches)
                snapshots.push_back(pair.first->serialize());

            for (size_t i = 0; i < matches.size(); ++i)
            {
                kObject *obj    = matches[i].first;
                kObject *parent = matches[i].second;

                // Drop selection if it points into the subtree being freed.
                if (selectedObject == obj)
                    selectedObject = nullptr;
                selectedObjects.erase(std::remove(selectedObjects.begin(), selectedObjects.end(),
                                                  obj->getUuid()),
                                      selectedObjects.end());

                // Detach from parent (scene root or another object), then delete.
                obj->detachFromParent();
                deleteObjectRecursive(obj);

                // Reload. Top-level objects (parent is scene root) pass nullptr
                // so loadObjectFromJson calls scene->addMesh. Nested objects
                // pass the original parent so the new mesh is re-parented.
                bool isTopLevel = (parent == s->getRootNode() || parent == nullptr);
                loadObjectFromJson(snapshots[i], s, w, am, projectPath,
                                   editorCamera, isTopLevel ? nullptr : parent);
                any = true;
            }
        }
        return any;
    };

    kAssetManager *am = getAssetManager();
    bool any = processScenes(world, am);

    // Also process the prefab world when prefab editing is active.
    if (prefabEditing && prefabWorld)
    {
        kDriver *savedDriver = kDriver::getCurrent();
        if (prefabRenderer && prefabRenderer->getDriver())
        {
            prefabRenderer->getDriver()->makeCurrent(window);
            kDriver::setCurrent(prefabRenderer->getDriver());
        }
        any |= processScenes(prefabWorld, prefabAssetManager ? prefabAssetManager : am);
        if (savedDriver)
        {
            savedDriver->makeCurrent(window);
            kDriver::setCurrent(savedDriver);
        }
    }

    pendingMeshReloads.clear();

    if (any)
    {
        reapplyStoredMaterials();
        if (panelHierarchy)
            panelHierarchy->refreshList();
    }
}

kShader *Manager::getRawShader(const kString &shaderUuid)
{
    if (shaderUuid.empty())
        return nullptr;
    auto cit = shaderCache.find(shaderUuid);
    if (cit != shaderCache.end())
        return cit->second;

    kShader *shader = nullptr;
    auto fit = fileMap.find(shaderUuid);
    if (fit != fileMap.end() && fit->second.type == "shader")
    {
        fs::path path = projectPath / "Assets" / fit->second.path;

        // Shader source is never translated by the engine: each backend loads its
        // own variant.  On DirectX 11 the .hlsl sibling of the raw shader asset is
        // used when it exists; otherwise the GLSL file is loaded and the D3D11
        // driver reports that it cannot compile GLSL.
        kDriver *activeDriver = kDriver::getCurrent();
        const bool backendIsD3D11 =
            (activeDriver != nullptr &&
             activeDriver->getRendererType() == kRendererType::RENDERER_D3D11);

        if (backendIsD3D11)
        {
            fs::path hlslPath = path;
            hlslPath.replace_extension(".hlsl");
            if (fs::exists(hlslPath))
                path = hlslPath;
        }

        std::ifstream f(path);
        if (f)
        {
            std::stringstream ss;
            ss << f.rdbuf();
            shader = new kShader();
            if (backendIsD3D11)
                shader->loadHlslCodeDX11(ss.str());
            else
                shader->loadGlslCode(ss.str());
            if (shader->getShaderProgram() == 0)
            {
                delete shader;
                shader = nullptr;
            }
        }
    }
    shaderCache[shaderUuid] = shader; // cache misses too, to avoid re-probing
    return shader;
}

kString Manager::getMaterialShaderSource(const nlohmann::json &matJson)
{
    kString shaderUuid = matJson.value("shader_uuid", std::string(""));
    if (!shaderUuid.empty())
    {
        auto fit = fileMap.find(shaderUuid);
        if (fit != fileMap.end() && fit->second.type == "shader")
        {
            std::ifstream f(projectPath / "Assets" / fit->second.path);
            if (f)
            {
                std::stringstream ss;
                ss << f.rdbuf();
                return ss.str();
            }
        }
        return "";
    }
    kString resName = builtinShaderResource(matJson.value("shader", std::string("Phong")));
    return resName.empty() ? kString("") : getEmbeddedResourceText(resName);
}

kMaterial *Manager::buildMaterialFromJson(const nlohmann::json &matJson)
{
    kAssetManager *am = getAssetManager();
    if (!am)
        return nullptr;

    // Resolve the shader: a referenced raw shader asset, else a built-in.
    kString shaderUuid = matJson.value("shader_uuid", std::string(""));
    kShader *shader = nullptr;
    if (!shaderUuid.empty())
    {
        shader = getRawShader(shaderUuid);
    }
    else
    {
        kString resName = builtinShaderResource(matJson.value("shader", std::string("Phong")));
        if (resName.empty())
            resName = "SHADER_MESH_PHONG";
        shader = am->loadGlslFromResource(resName.c_str());
    }
    if (!shader || shader->getShaderProgram() == 0)
        return nullptr;

    kString source = getMaterialShaderSource(matJson);

    // The material itself is caller-owned (the preview deletes it each rebuild;
    // object assignment keeps it). Its shader and textures stay asset-manager-
    // owned, so deleting a material never frees those shared GPU resources.
    kMaterial *mat = new kMaterial();
    mat->setShader(shader);

    const nlohmann::json *params =
        (matJson.contains("params") && matJson["params"].is_object()) ? &matJson["params"] : nullptr;

    // Legacy top-level .mat key for a built-in @var name, so materials saved
    // before the @var system keep their look until re-saved.
    auto legacyKey = [](const kString &name) -> kString
    {
        if (name == "material.diffuse")
            return "diffuse";
        if (name == "material.ambient")
            return "ambient";
        if (name == "material.specular")
            return "specular";
        if (name == "material.shininess")
            return "shininess";
        if (name == "material.metallic")
            return "metallic";
        if (name == "material.roughness")
            return "roughness";
        if (name == "material.glossiness")
            return "glossiness";
        if (name == "material.tiling")
            return "uv_tiling";
        if (name == "albedoMap")
            return "texture_albedo";
        if (name == "normalMap")
            return "texture_normal";
        if (name == "specularMap")
            return "texture_specular";
        if (name == "glossinessMap")
            return "texture_glossiness";
        if (name == "emissiveMap")
            return "texture_emissive";
        return "";
    };
    auto valueFor = [&](const ShaderVar &v) -> const nlohmann::json *
    {
        if (params && params->contains(v.name))
            return &params->at(v.name);
        kString lk = legacyKey(v.name);
        if (!lk.empty() && matJson.contains(lk))
            return &matJson.at(lk);
        return nullptr;
    };
    auto readVecN = [](const nlohmann::json *j, int n, kVec4 def) -> kVec4
    {
        if (j && j->is_array() && (int)j->size() >= n)
            for (int i = 0; i < n && i < 4; ++i)
                def[i] = (*j)[i].get<float>();
        return def;
    };

    for (const ShaderVar &v : parseShaderVars(source))
    {
        const nlohmann::json *j = valueFor(v);
        if (v.type == "float")
        {
            float def = (v.name == "material.shininess")    ? 32.0f
                        : (v.name == "material.roughness")  ? 0.5f
                        : (v.name == "material.glossiness") ? 1.0f
                                                            : 0.0f;
            mat->setParamFloat(v.name, (j && j->is_number()) ? j->get<float>() : def);
        }
        else if (v.type == "int")
            mat->setParamInt(v.name, (j && j->is_number()) ? j->get<int>() : 0);
        else if (v.type == "bool")
            mat->setParamBool(v.name, (j && j->is_boolean()) ? j->get<bool>() : false);
        else if (v.type == "vec2")
        {
            kVec4 c = readVecN(j, 2, kVec4(1, 1, 0, 0));
            mat->setParamVec2(v.name, kVec2(c.x, c.y));
        }
        else if (v.type == "vec3")
        {
            kVec4 c = readVecN(j, 3, kVec4(1, 1, 1, 0));
            mat->setParamVec3(v.name, kVec3(c.x, c.y, c.z));
        }
        else if (v.type == "vec4")
            mat->setParamVec4(v.name, readVecN(j, 4, kVec4(1, 1, 1, 1)));
        else if (v.type == "sampler2D" || v.type == "samplerCube")
        {
            kString uuid = (j && j->is_string()) ? j->get<std::string>() : kString("");
            kTexture2D *tex = getProjectTexture(uuid, v.name);
            if (tex)
            {
                if (v.type == "sampler2D")
                    mat->setParamSampler2D(v.name, tex);
                else
                    mat->setParamSamplerCube(v.name, tex);
            }
        }
    }

    mat->setSingleSided(matJson.value("single_sided", true));

    // [TEMP DIAGNOSTIC] Dump what the build produced, to chase params/textures
    // not taking effect. Remove once resolved.
    {
        static int s_dbg = 0;
        if (s_dbg++ < 40)
        {
            std::ofstream log("d:/Projects/Kemena3D/material_debug.log", std::ios::app);
            log << "buildMaterial shader=" << matJson.value("shader", std::string("?"))
                << " srcLen=" << source.size()
                << " vars=" << parseShaderVars(source).size()
                << " params=" << mat->getParams().size() << "\n";
            for (const auto &kv : mat->getParams())
            {
                log << "   " << kv.first << " type=" << (int)kv.second.type;
                if (kv.second.type == kMaterialParamType::SAMPLER2D ||
                    kv.second.type == kMaterialParamType::SAMPLERCUBE)
                    log << " tex=" << (kv.second.texture ? kv.second.texture->getTextureID() : 0);
                else
                    log << " val=(" << kv.second.value.x << "," << kv.second.value.y << ","
                        << kv.second.value.z << ")";
                log << "\n";
            }
        }
    }
    return mat;
}

bool Manager::applyMaterialToObject(kObject *obj, const fs::path &materialPath,
                                    const kString &materialUuid)
{
    if (!obj)
        return false;
    kAssetManager *am = getAssetManager();
    if (!am)
        return false;

    // Read the .mat JSON and build a runtime kMaterial. Mirrors
    // PanelInspector::rebuildMatViewMaterial — kept duplicated here to avoid
    // pulling the inspector into Manager's dependency surface.
    json matJson;
    try
    {
        std::ifstream f(materialPath);
        if (!f.is_open())
            return false;
        matJson = json::parse(f);
    }
    catch (...)
    {
        return false;
    }

    kMaterial *mat = buildMaterialFromJson(matJson);
    if (!mat)
        return false;

    // Apply to THIS node only (not its children). Material assignment is now
    // per-sub-mesh: drag-drop targets the exact sub-mesh hit by Object ID, and
    // the inspector picker targets the selected mesh. The geometry-less parent
    // of an imported model is never the target.
    obj->setMaterial(mat, /*setChildren*/ false);
    // Record the source asset UUID on the root so the assignment is persisted
    // on save and can be re-applied (and re-propagated) on load.
    obj->setMaterialUuid(materialUuid);
    projectSaved = false;
    refreshWindowTitle();
    return true;
}

void Manager::applyDefaultMaterialToObject(kObject *obj)
{
    if (!obj)
        return;
    kAssetManager *am = getAssetManager();
    if (!am)
        return;

    // Mirrors the default material assigned to freshly-created meshes.
    kShader *shader = am->loadGlslFromResource("SHADER_MESH_PHONG");
    kMaterial *mat = am->createMaterial(shader);
    mat->setAmbientColor(kVec3(1.0f, 1.0f, 1.0f));
    mat->setDiffuseColor(kVec3(0.5f, 0.5f, 0.5f));
    obj->setMaterial(mat, /*setChildren*/ false);
    obj->setMaterialUuid("");
    projectSaved = false;
    refreshWindowTitle();
}

// Recursively re-apply the stored material UUID for a node and its descendants.
static void reapplyMaterialsRecursive(Manager *mgr, kObject *node,
                                      const fs::path &projectPath)
{
    if (!node)
        return;
    const kString mu = node->getMaterialUuid();
    if (!mu.empty())
    {
        auto it = mgr->fileMap.find(mu);
        if (it != mgr->fileMap.end() && it->second.type == "material")
            mgr->applyMaterialToObject(node, projectPath / "Assets" / it->second.path, mu);
        // else: asset missing — keep the UUID, leave the default material.
    }
    for (kObject *child : node->getChildren())
        reapplyMaterialsRecursive(mgr, child, projectPath);
}

void Manager::reapplyStoredMaterials()
{
    if (!world)
        return;
    for (kScene *s : world->getScenes())
        if (s && s->getRootNode())
            for (kObject *child : s->getRootNode()->getChildren())
                reapplyMaterialsRecursive(this, child, projectPath);
}

static void captureMaterialSubtreeRec(kObject *node, std::vector<MaterialSnapshot> &out)
{
    if (!node)
        return;
    out.push_back({node->getUuid(), node->getMaterial(), node->getMaterialUuid()});
    for (kObject *c : node->getChildren())
        captureMaterialSubtreeRec(c, out);
}

std::vector<MaterialSnapshot> Manager::captureMaterialSubtree(kObject *root)
{
    std::vector<MaterialSnapshot> snap;
    captureMaterialSubtreeRec(root, snap);
    return snap;
}

void Manager::restoreMaterialSubtree(const std::vector<MaterialSnapshot> &snap)
{
    for (const MaterialSnapshot &s : snap)
    {
        kObject *obj = findObjectByUuid(s.uuid);
        if (!obj)
            continue;
        obj->setMaterial(s.material, /*setChildren*/ false);
        obj->setMaterialUuid(s.materialUuid);
    }
    projectSaved = false;
    refreshWindowTitle();
}

// Free helper so the static loadObjectFromJson (which has no Manager) can use it.
static void assignImportChildUuidsRec(kObject *root)
{
    if (!root)
        return;
    for (kObject *c : root->getChildren())
    {
        // UUID-less descendants are the importer's sub-meshes: give them a
        // stable id and flag them so they stay out of serialization but become
        // selectable/duplicable in the hierarchy.
        if (c->getUuid().empty())
        {
            c->setUuid(generateUuid());
            c->setImportChild(true);
        }
        assignImportChildUuidsRec(c);
    }
}

void Manager::assignImportChildUuids(kObject *root)
{
    assignImportChildUuidsRec(root);
}

void Manager::deleteAssets(const std::vector<fs::path> &paths)
{
    for (const auto &p : paths)
    {
        if (!fs::exists(p))
            continue;

        std::error_code ec;
        if (fs::is_directory(p))
            fs::remove_all(p, ec);
        else
            fs::remove(p, ec);

        if (ec)
            std::cerr << "Failed to delete " << p << ": " << ec.message() << "\n";
    }

    checkAssetChange();
}

void Manager::reimportAsset(const kString &uuid)
{
    if (uuid.empty() || projectPath.empty())
        return;

    // Delete the converted file in Library/ImportedAssets and thumbnail/metadata
    // so checkAssetChange will re-import from the source.
    fs::path libDir = projectPath / "Library" / "ImportedAssets";

    // Try common extensions for mesh/image/audio
    static const char* kExts[] = {".glb", ".dds", ".wav", ".ogg", ".mp3", ".flac"};
    for (const char* ext : kExts)
    {
        fs::path cand = libDir / (uuid + ext);
        if (fs::exists(cand))
        {
            std::error_code ec;
            fs::remove(cand, ec);
            break;
        }
    }

    // Remove thumbnail and metadata to force regeneration
    auto tryRemove = [](const fs::path &p) {
        if (fs::exists(p)) { std::error_code ec; fs::remove(p, ec); }
    };
    tryRemove(projectPath / "Library" / "Thumbnails" / (uuid + ".png"));
    tryRemove(projectPath / "Library" / "Metadata" / (uuid + ".json"));

    // Trigger re-import
    checkAssetChange();
}

// ===========================================================================
// Animator controller runtime
// ===========================================================================

static kMesh *findFirstMeshInSubtree(kObject *node)
{
    if (!node)
        return nullptr;
    if (node->getType() == NODE_TYPE_MESH)
        return static_cast<kMesh *>(node);
    for (kObject *child : node->getChildren())
    {
        kMesh *mesh = findFirstMeshInSubtree(child);
        if (mesh)
            return mesh;
    }
    return nullptr;
}

static void collectBoneMeshes(kMesh *mesh, std::vector<kMesh *> &out)
{
    if (!mesh)
        return;
    if (mesh->getBoneCount() > 0)
        out.push_back(mesh);
    for (kObject *child : mesh->getChildren())
        if (child && child->getType() == NODE_TYPE_MESH)
            collectBoneMeshes(static_cast<kMesh *>(child), out);
}

static void animatorDebugLog(const std::string &msg)
{
    std::cerr << msg << "\n";
    std::ofstream f("D:\\Projects\\Kemena3D\\animator_debug.log", std::ios::app);
    if (f.is_open())
        f << msg << std::endl;
}

static float readMeshScaleFactor(const fs::path &projectPath, const std::string &uuid)
{
    if (uuid.empty())
        return 1.0f;

    fs::path metaPath = projectPath / "Library" / "Metadata" / (uuid + ".json");
    if (!fs::exists(metaPath))
        return 1.0f;

    try
    {
        std::ifstream mf(metaPath);
        nlohmann::json mj;
        mf >> mj;
        return mj.value("scaleFactor", 1.0f);
    }
    catch (...)
    {
    }
    return 1.0f;
}

static AnimState *findDefaultAnimatorState(AnimatorGraph &graph)
{
    for (auto &s : graph.states)
        if (s.isState() && s.isDefault)
            return &s;
    for (auto &s : graph.states)
        if (s.isState())
            return &s;
    return nullptr;
}

static AnimState *resolveAnimatorPassthrough(RuntimeAnimator &rt, AnimState *target)
{
    std::set<int> visited;
    while (target && target->isAnchor() && !visited.count(target->id))
    {
        visited.insert(target->id);
        AnimState *next = nullptr;
        for (auto &t : rt.graph->transitions)
        {
            if (t.fromStateId != target->id)
                continue;
            next = rt.graph->findState(t.toStateId);
            if (next)
                break;
        }
        target = next;
    }
    return target;
}

static void enterAnimatorState(RuntimeAnimator &rt, AnimState *state)
{
    if (!state)
        return;
    rt.currentStateId = state->id;
    rt.stateTimeSeconds = 0.0f;
    auto it = rt.clipForState.find(state->animationUuid);
    if (it != rt.clipForState.end() && rt.animator)
        rt.animator->playAnimation(it->second);
}

static AnimState *evaluateAnimatorTransitions(RuntimeAnimator &rt, AnimState *state, float animSeconds,
                                              AnimTransition **outFired)
{
    if (!state)
        return nullptr;

    for (auto &t : rt.graph->transitions)
    {
        if (t.fromStateId != state->id)
            continue;
        if (t.hasExitTime && animSeconds < t.exitTime)
            continue;

        bool conditionsMet = true;
        for (auto &c : t.conditions)
        {
            if (!c.evaluate(rt.animator ? rt.animator->getVariables() : rt.variables))
            {
                conditionsMet = false;
                break;
            }
        }

        if (conditionsMet)
        {
            AnimState *target = rt.graph->findState(t.toStateId);
            target = resolveAnimatorPassthrough(rt, target);
            if (target && target->id != state->id && target->isState())
            {
                // Never transition into a state with no playable clip. The
                // graph would get stuck there (stepAnimators skips states whose
                // clip failed to load, before transitions are re-evaluated) and
                // the object would freeze in place. Skip such edges so the
                // current state keeps playing instead.
                if (rt.clipForState.count(target->animationUuid) == 0)
                    continue;
                if (outFired)
                    *outFired = &t;
                return target;
            }
        }
    }
    return nullptr;
}

static bool buildRuntimeAnimator(Manager *mgr, kObject *obj)
{
    kString ref = obj->getAnimatorRef();
    if (ref.empty())
        return false;

    fs::path animPath = mgr->findAssetPathByUuid(ref);
    if (animPath.empty() || !fs::exists(animPath))
    {
        animatorDebugLog("[Animator] build: animator path missing for ref=" + ref);
        return false;
    }
    animatorDebugLog("[Animator] build: obj=" + obj->getUuid() + " animPath=" + animPath.string());

    nlohmann::json j;
    try
    {
        std::ifstream f(animPath);
        if (!f.is_open())
            return false;
        f >> j;
    }
    catch (...)
    {
        return false;
    }

    RuntimeAnimator rt;
    rt.objectUuid = obj->getUuid();
    rt.graph = std::make_shared<AnimatorGraph>();
    rt.graph->fromJson(j);

    kMesh *rootMesh = findFirstMeshInSubtree(obj);
    if (!rootMesh)
        return false;

    kAssetManager *am = mgr->getAssetManager();
    if (!am)
        return false;

    for (auto &v : rt.graph->variables)
        rt.variables[v.name] = v.defaultValue;

    // Load the clip referenced by each state. Clips for one model resolve to
    // the same imported GLB; one load per animation UUID is sufficient.
    for (auto &st : rt.graph->states)
    {
        if (st.animationUuid.empty())
            continue;
        if (rt.clipForState.count(st.animationUuid))
            continue;

        fs::path clipPath = mgr->findAssetPathByUuid(st.animationUuid);
        if (clipPath.empty() || !fs::exists(clipPath))
            continue;

        nlohmann::json aj;
        try
        {
            std::ifstream af(clipPath);
            if (!af.is_open())
                continue;
            af >> aj;
        }
        catch (...)
        {
            continue;
        }

        std::string meshUuid = aj.value("meshUuid", std::string());
        if (meshUuid.empty())
            continue;

        float startFrame = aj.value("startFrame", 0.0f);
        float endFrame   = aj.value("endFrame", 30.0f);
        rt.clipFrames[st.animationUuid] = { startFrame, endFrame };

        // Root-motion options from the .animation asset: which channels are
        // extracted for scripts and baked out of the pose (see kAnimator).
        bool rootMotionRotation = aj.value("rootMotionRotation", false);
        bool rootMotionPositionY = aj.value("rootMotionPositionY", false);
        bool rootMotionPositionXZ = aj.value("rootMotionPositionXZ", false);
        rt.clipRootMotion[st.animationUuid] = { rootMotionRotation, rootMotionPositionY, rootMotionPositionXZ };

        fs::path glbPath = mgr->projectPath / "Library" / "ImportedAssets" / (meshUuid + ".glb");
        if (!fs::exists(glbPath))
            continue;

        try
        {
            kSkeletalAnimation *clip = am->loadAnimation(glbPath.string(), rootMesh);
            if (!clip)
                continue;
            // stepAnimators drives time manually; zero speed stops the main
            // kRenderer from advancing the same clip a second time each frame.
            clip->setSpeed(0.0f);

            // Apply the .animation asset's root-motion options so scripts can
            // read the extracted deltas and the pose is baked accordingly.
            auto rmIt = rt.clipRootMotion.find(st.animationUuid);
            if (rmIt != rt.clipRootMotion.end())
            {
                clip->setRootMotionRotation(rmIt->second[0]);
                clip->setRootMotionPositionY(rmIt->second[1]);
                clip->setRootMotionPositionXZ(rmIt->second[2]);
            }
            animatorDebugLog("[Animator] rootMotion clip=" + st.animationUuid +
                             " read(rot,y,xz)=(" +
                             std::to_string(rootMotionRotation ? 1 : 0) + "," +
                             std::to_string(rootMotionPositionY ? 1 : 0) + "," +
                             std::to_string(rootMotionPositionXZ ? 1 : 0) + ")" +
                             " clip(rot,y,xz)=(" +
                             std::to_string(clip->getRootMotionRotation() ? 1 : 0) + "," +
                             std::to_string(clip->getRootMotionPositionY() ? 1 : 0) + "," +
                             std::to_string(clip->getRootMotionPositionXZ() ? 1 : 0) + ")");

            // Compensate for unit-scale differences between the animation asset
            // and the mesh it is bound to. Both are imported independently and
            // may use different scaleFactor settings; without this, a clip
            // imported at scale 1.0 bound to a mesh imported at scale 0.02
            // produces wildly wrong translations and the mesh looks exploded.
            float meshScale = readMeshScaleFactor(mgr->projectPath, rootMesh->getRefName());
            float animScale = readMeshScaleFactor(mgr->projectPath, meshUuid);
            if (meshScale > 0.0f && animScale > 0.0f)
            {
                float ratio = meshScale / animScale;
                if (std::fabs(ratio - 1.0f) > 1e-5f)
                    clip->applyTranslationScale(ratio);
                std::string dbg = "[Animator] clip=" + st.animationUuid +
                                  " tps=" + std::to_string(clip->getTicksPerSecond()) +
                                  " dur=" + std::to_string(clip->getDuration()) +
                                  " meshScale=" + std::to_string(meshScale) +
                                  " animScale=" + std::to_string(animScale) +
                                  " ratio=" + std::to_string(ratio);
                animatorDebugLog(dbg);
            }

            rt.ownedClips.push_back(clip);
            rt.clipForState[st.animationUuid] = clip;
        }
        catch (const std::exception &)
        {
            // GLB without a skeletal clip — treat the state as unplayable.
        }
    }

    if (rt.clipForState.empty())
        return false;

    rt.rootMesh = rootMesh;
    rt.animator = new kAnimator(nullptr);

    // Scripts set animator controller variables through kAnimator; seed the
    // runtime values from the graph defaults here.
    for (const auto &v : rt.graph->variables)
        rt.animator->setVariable(v.name, v.defaultValue);

    // Enter a playable state immediately so the renderer never observes a
    // kAnimator whose currentAnimation is nullptr.
    AnimState *defaultState = findDefaultAnimatorState(*rt.graph);
    if (defaultState && rt.clipForState.count(defaultState->animationUuid) == 0)
    {
        for (auto &st : rt.graph->states)
            if (st.isState() && rt.clipForState.count(st.animationUuid))
            {
                defaultState = &st;
                break;
            }
    }
    if (!defaultState)
    {
        delete rt.animator;
        for (kSkeletalAnimation *clip : rt.ownedClips)
            delete clip;
        return false;
    }
    enterAnimatorState(rt, defaultState);

    std::vector<kMesh *> boneMeshes;
    collectBoneMeshes(rootMesh, boneMeshes);
    for (kMesh *m : boneMeshes)
        m->setAnimator(rt.animator);

    mgr->runtimeAnimators.push_back(std::move(rt));
    return true;
}

static void startAnimatorsRecursive(Manager *mgr, kObject *node)
{
    if (!node)
        return;
    if (!node->getAnimatorRef().empty())
        buildRuntimeAnimator(mgr, node);
    for (kObject *child : node->getChildren())
        startAnimatorsRecursive(mgr, child);
}

static void detachRuntimeAnimatorFromMesh(kMesh *mesh, kAnimator *animator)
{
    if (!mesh)
        return;
    if (mesh->getAnimator() == animator)
    {
        mesh->setAnimator(nullptr);
        mesh->setSkinned(false);
    }
    for (kObject *child : mesh->getChildren())
        if (child && child->getType() == NODE_TYPE_MESH)
            detachRuntimeAnimatorFromMesh(static_cast<kMesh *>(child), animator);
}

void Manager::startAnimators()
{
    stopAnimators();

    animatorDebugLog("[Animator] startAnimators: projectOpened=" + std::to_string(projectOpened ? 1 : 0));

    if (!projectOpened || !world)
        return;

    for (kScene *s : world->getScenes())
    {
        if (!s || !s->getRootNode())
            continue;
        for (kObject *child : s->getRootNode()->getChildren())
            startAnimatorsRecursive(this, child);
    }

    animatorDebugLog("[Animator] startAnimators done: runtimeCount=" + std::to_string(runtimeAnimators.size()));
}

void Manager::stopAnimators()
{
    for (auto &rt : runtimeAnimators)
    {
        if (rt.animator && rt.rootMesh)
            detachRuntimeAnimatorFromMesh(rt.rootMesh, rt.animator);
        delete rt.animator;
        for (kSkeletalAnimation *clip : rt.ownedClips)
            delete clip;
    }
    runtimeAnimators.clear();
}

void Manager::stepAnimators(float dt)
{
    const float kAnimFps = 30.0f;

    for (auto &rt : runtimeAnimators)
    {
        if (!rt.animator)
            continue;

        // Advance any active cross-fade before evaluating state transitions so
        // a blend started this frame isn't advanced until the next one.
        if (rt.animator->isBlending())
            rt.animator->updateBlend(dt);

        AnimState *state = rt.graph->findState(rt.currentStateId);
        if (!state)
        {
            state = findDefaultAnimatorState(*rt.graph);
            if (!state)
                continue;
            enterAnimatorState(rt, state);
        }

        auto clipIt = rt.clipForState.find(state->animationUuid);
        if (clipIt == rt.clipForState.end() || !clipIt->second)
            continue;

        kSkeletalAnimation *clip = clipIt->second;
        auto frameIt = rt.clipFrames.find(state->animationUuid);
        float startFrame = (frameIt != rt.clipFrames.end()) ? frameIt->second.first : 0.0f;
        float endFrame   = (frameIt != rt.clipFrames.end()) ? frameIt->second.second : 0.0f;

        float startSec = startFrame / kAnimFps;
        float endSec   = endFrame / kAnimFps;

        rt.stateTimeSeconds += dt;
        float animSeconds = startSec + rt.stateTimeSeconds * state->speed;

        if (state->loop)
        {
            float dur = endSec - startSec;
            if (dur > 1e-4f)
                animSeconds = startSec + std::fmod(animSeconds - startSec, dur);
        }
        else if (animSeconds >= endSec)
        {
            animSeconds = endSec;
        }

        AnimTransition *firedTrans = nullptr;
        AnimState *next = evaluateAnimatorTransitions(rt, state, animSeconds, &firedTrans);
        if (next)
        {
            // Capture the source clip and its current pose time so a cross-fade
            // can blend from the exact pose the transition fired at.
            kSkeletalAnimation *fromClip = clip;
            const float fromTicks = animSeconds * clip->getTicksPerSecond();

            enterAnimatorState(rt, next);
            state = next;
            clipIt = rt.clipForState.find(state->animationUuid);
            if (clipIt == rt.clipForState.end() || !clipIt->second)
                continue;
            clip = clipIt->second;
            frameIt = rt.clipFrames.find(state->animationUuid);
            startFrame = (frameIt != rt.clipFrames.end()) ? frameIt->second.first : 0.0f;
            endFrame   = (frameIt != rt.clipFrames.end()) ? frameIt->second.second : 0.0f;
            animSeconds = startFrame / kAnimFps;

            // Honour the transition's blend settings: cross-fade over
            // blendDuration seconds (the editor default), or snap instantly when
            // the transition uses AnimBlendMode::Instant / a zero duration.
            const bool crossFade = firedTrans &&
                                   firedTrans->blendMode == AnimBlendMode::CrossFade &&
                                   firedTrans->blendDuration > 0.01f &&
                                   fromClip != nullptr && fromClip != clip;
            if (crossFade)
            {
                const float toTicks = animSeconds * clip->getTicksPerSecond();
                rt.animator->beginBlend(fromClip, fromTicks, clip, toTicks,
                                        firedTrans->blendDuration);
            }
        }

        float ticks = animSeconds * clip->getTicksPerSecond();
        rt.animator->setCurrentTime(ticks);

        try
        {
            const kNodeData &root = clip->getRootNode();
            rt.animator->calculateBoneTransform(&root, kMat4(1.0f));
        }
        catch (const std::exception &)
        {
            // Keep the last successfully computed pose.
        }

        {
            static int dbg = 0;
            if (dbg < 10 || (dbg % 120 == 0))
            {
                // Diagnostic: report whether the pose actually reached the mesh.
                int nonIdentity = 0;
                kVec3 sample(0.0f);
                const auto &finalMats = rt.animator->getFinalBoneMatrices();
                for (const kMat4 &m : finalMats)
                {
                    kVec3 t = kVec3(m[3][0], m[3][1], m[3][2]);
                    bool moved = std::fabs(t.x) > 1e-5f || std::fabs(t.y) > 1e-5f ||
                                 std::fabs(t.z) > 1e-5f;
                    if (moved)
                    {
                        nonIdentity++;
                        if (nonIdentity == 1)
                            sample = t;
                    }
                }
                // NOTE: do NOT call getRootMotionDeltaPosition()/Rotation() here
                // — they consume the accumulator and would steal deltas from
                // scripts. Only the flags and resolved root bone are logged.
                std::string msg = "[Animator] step dt=" + std::to_string(dt) +
                                  " stateTime=" + std::to_string(rt.stateTimeSeconds) +
                                  " animSec=" + std::to_string(animSeconds) +
                                  " ticks=" + std::to_string(ticks) +
                                  " tps=" + std::to_string(clip->getTicksPerSecond()) +
                                  " skinned=" + std::to_string(rt.rootMesh && rt.rootMesh->getSkinned() ? 1 : 0) +
                                  " animator=" + std::to_string(rt.rootMesh && rt.rootMesh->getAnimator() ? 1 : 0) +
                                  " nonIdentityBones=" + std::to_string(nonIdentity) +
                                  " sample=(" + std::to_string(sample.x) + "," +
                                                std::to_string(sample.y) + "," +
                                                std::to_string(sample.z) + ")" +
                                  " rootMotion=" + std::to_string(rt.animator->isRootMotionActive() ? 1 : 0) +
                                  " rmBone='" + rt.animator->getResolvedRootBoneName() + "'" +
                                  " rm(rot,y,xz)=(" +
                                  std::to_string(rt.animator->getRootMotionRotation() ? 1 : 0) + "," +
                                  std::to_string(rt.animator->getRootMotionPositionY() ? 1 : 0) + "," +
                                  std::to_string(rt.animator->getRootMotionPositionXZ() ? 1 : 0) + ")";
                animatorDebugLog(msg);
            }
            dbg++;
        }
    }
}
