#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include "kemena/kemena.h"
#include "manager.h"

using namespace kemena;

/**
 * @brief Modal start-up splash window for the Kemena3D editor.
 *
 * Renders a centered ImGui overlay that dims the rest of the editor and
 * presents the branding splash image, logo, "New Project"/"Open Project"
 * actions and a list of recent projects. Project actions are delegated to
 * the owning Manager. The splash closes when an action succeeds or when the
 * user clicks outside its bounds.
 */
class SplashScreen
{
public:
    /**
     * @brief Construct the splash screen and load its textures.
     *
     * Loads the splash background and logo images from embedded resources via
     * the asset manager and caches their ImGui texture references.
     *
     * @param gui          GUI manager used for ImGui rendering.
     * @param assetManager Asset manager used to load the splash/logo textures.
     * @param manager      Editor manager that performs project operations and
     *                     supplies the recent-projects list.
     */
    SplashScreen(kGuiManager* gui, kAssetManager* assetManager, Manager* manager);

    /**
     * @brief Render one frame of the splash screen.
     *
     * Draws the dimmed backdrop, splash image, logo, action buttons and recent
     * project entries, and handles their input. Triggers project creation/open
     * on the Manager and closes the splash on success or on an outside click.
     *
     * @return True while the splash remains open, false once it has closed.
     */
    bool draw();

    /** @brief Whether the splash screen is currently open/visible. */
    bool isOpen() const { return open; }

    /** @brief Make the splash screen visible again. */
    void show()         { open = true; }

private:
    kGuiManager*  gui;                    ///< GUI manager used for ImGui rendering.
    Manager*      manager;                ///< Editor manager for project actions and recent list.
    ImTextureRef  texSplash   = nullptr;  ///< ImGui texture reference for the splash background.
    ImTextureRef  texLogo     = nullptr;  ///< ImGui texture reference for the logo.
    unsigned int  splashGlId  = 0;        ///< OpenGL texture id of the splash background.
    unsigned int  logoGlId    = 0;        ///< OpenGL texture id of the logo.
    bool          open        = true;     ///< True while the splash is shown.
};

#endif // SPLASH_SCREEN_H
