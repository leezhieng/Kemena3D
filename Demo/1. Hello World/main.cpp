#include "kemena/kemena.h"

using namespace kemena;

int main()
{
    // Create window and renderer
    kWindow* window = createWindow(1024, 768, "Kemena3D Demo - Hello World");
    kRenderer* renderer = createRenderer(window);
    renderer->setClearColor(kVec4(0.4f, 0.6f, 0.8f, 1.0f));

    // Create the asset manager, world and scene
    kAssetManager* assetManager = createAssetManager();
    kWorld* world = createWorld(assetManager);
    kScene* scene = world->createScene("My Scene");

    // Create a camera. Cameras now live on the world (not the scene); make it
    // the active view camera the renderer draws through.
    kCamera* camera = world->addCamera(kVec3(2.5f, 2.5f, 2.5f), kVec3(0.0f, 0.5f, 0.0f),
                                       kCameraType::CAMERA_TYPE_LOCKED);
    world->setMainCamera(camera);

    // Load the flat (unlit) shader from a single combined .glsl source and build
    // a textured material. The file uses "// --- VERTEX ---" / "// --- FRAGMENT ---"
    // markers, split by loadGlslFile().
    kShader* shader = new kShader();
    shader->loadGlslFile("../flat.glsl");

    kMaterial* mat = assetManager->createMaterial(shader);
    kTexture2D* diff = assetManager->loadTexture2D("../diffuse.png", "albedoMap");
    mat->addTexture(diff);

    // Load a 3D model and apply the material to it
    kMesh* mesh = scene->addMesh("../reptile_mage.obj");
    mesh->setRotation(kQuat(kVec3(0.0f, -0.4f, 0.0f))); // euler radians -> quaternion
    mesh->setMaterial(mat);

    // Game loop
    kSystemEvent event;
    while (window->getRunning())
    {
        if (event.hasEvent())
        {
            if (event.getType() == K_EVENT_QUIT)
            {
                window->setRunning(false);
            }
        }

        // render() now takes the world as well as the scene, and resolves the
        // viewpoint from world->getMainCamera().
        renderer->render(world, scene, 0, 0,
                         window->getWindowWidth(), window->getWindowHeight(),
                         window->getTimer()->getDeltaTime());
    }

    // Clean up
    renderer->destroy();
    window->destroy();
    return 0;
}
