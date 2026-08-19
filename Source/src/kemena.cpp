#include "kemena.h"

namespace kemena
{
    kWindow *createWindow(int width, int height, kString title)
    {
        kWindow *window = new kWindow;
        bool done = window->init(width, height, title);
        if (done)
            return window;
        else
            return nullptr;
    }

    kWindow *createWindow(int width, int height, kString title, bool maximized, kWindowType type, void *nativeHandle)
    {
        kWindow *window = new kWindow;
        bool done = window->init(width, height, title, maximized, type, nativeHandle);
        if (done)
            return window;
        else
            return nullptr;
    }

    kRenderer *createRenderer(kWindow *window)
    {
        kRenderer *renderer = new kRenderer;
        renderer->setEngineInfo(engineName, engineVersion);
        bool done = renderer->init(window);
        if (done)
            return renderer;
        else
            return nullptr;
    }

    kAssetManager *createAssetManager()
    {
        kAssetManager *manager = new kAssetManager();
        return manager;
    }

    kWorld *createWorld(kAssetManager *assetManager)
    {
        kWorld *manager = new kWorld();
        manager->setAssetManager(assetManager);
        return manager;
    }

    kScriptManager *createScriptManager()
    {
        kScriptManager *manager = new kScriptManager();
        return manager;
    }

    kGuiManager *createGuiManager(kRenderer *renderer)
    {
        kGuiManager *manager = new kGuiManager();
        manager->init(renderer);
        return manager;
    }

    kInputManager *createInputManager()
    {
        kInputManager *manager = new kInputManager();
        manager->init();
        return manager;
    }

    kAudioManager *createAudioManager()
    {
        kAudioManager *manager = new kAudioManager();
        manager->init();
        return manager;
    }

    kPhysicsManager *createPhysicsManager()
    {
        kPhysicsManager *manager = new kPhysicsManager();
        manager->init();
        return manager;
    }

    kNavManager *createNavManager(kNavMesh *mesh, int maxAgents)
    {
        kNavManager *manager = new kNavManager(mesh, maxAgents);
        manager->init();
        return manager;
    }
}
