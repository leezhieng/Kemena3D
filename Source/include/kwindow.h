/**
 * @file kwindow.h
 * @brief Application window management backed by SDL3.
 */

#ifndef KWINDOW_H
#define KWINDOW_H

#include "kexport.h"

#include "kdatatype.h"
#include "ktimer.h"

#include <iostream>
#include <string>

// Tell SDL not to mess with main()
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

namespace kemena
{
    /**
     * @brief Creates and manages an SDL3 application window.
     *
     * kWindow wraps the SDL_Window lifecycle and provides helpers for querying
     * window dimensions, swapping the back buffer, and accessing the built-in
     * frame timer. Attach a kRenderer to a kWindow before rendering.
     */
    class KEMENA3D_API kWindow
    {
    public:
        /** @brief Constructs an uninitialised window; call init() to create the SDL window. */
        kWindow();

        /** @brief Destroys the window, releasing the SDL window and frame timer. */
        virtual ~kWindow();

        /**
         * @brief Creates and shows the SDL window.
         * @param width        Initial client-area width in pixels.
         * @param height       Initial client-area height in pixels.
         * @param title        Window title bar text.
         * @param maximized    If true, the window starts maximised.
         * @param type         Window mode (windowed, fullscreen, borderless).
         * @param nativeHandle Optional platform-specific parent window handle.
         * @return true on success.
         */
        bool init(int width, int height, kString title, bool maximized = false,
                  kWindowType type = kWindowType::WINDOW_DEFAULT, void *nativeHandle = nullptr);

        /** @brief Destroys the SDL window and releases its resources. */
        void destroy();

        /** @brief Swaps the front and back buffers (presents the rendered frame). */
        void swap();

        /** @brief Returns the current client-area width in pixels. */
        int getWindowWidth();

        /** @brief Returns the current client-area height in pixels. */
        int getWindowHeight();

        /** @brief Returns the window title kString. */
        kString getWindowTitle();

        /**
         * @brief Changes the window title bar text.
         * @param newTitle New title kString.
         */
        void setWindowTitle(kString newTitle);

        /**
         * @brief Returns the underlying SDL_Window pointer.
         * @return Raw SDL window handle; valid until destroy() is called.
         */
        SDL_Window *getSdlWindow();

        /**
         * @brief Returns whether the application main loop should keep running.
         * @return false after the user closes the window or setRunning(false) is called.
         */
        bool getRunning();

        /**
         * @brief Controls the running state of the application.
         * @param newRunning Set to false to request the main loop to exit.
         */
        void setRunning(bool newRunning);

        /**
         * @brief Returns the frame timer associated with this window.
         * @return Pointer to the kTimer; valid for the lifetime of the window.
         */
        kTimer *getTimer();

    protected:
    private:
        SDL_Window *sdlWindow = NULL; ///< Underlying SDL window handle.

        kString windowTitle;  ///< Current window title.
        int windowWidth;     ///< Current client-area width in pixels.
        int windowHeight;    ///< Current client-area height in pixels.

        bool running = true; ///< Application loop control flag.
        kTimer *timer;       ///< Per-frame delta-time timer.
    };
}

#endif // KWINDOW_H
