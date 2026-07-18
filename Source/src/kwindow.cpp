#include "kwindow.h"

namespace kemena
{
    kWindow::kWindow()
    {
        timer = new kTimer();
    }

    kWindow::~kWindow()
    {
    }

    bool kWindow::init(int width, int height, kString title, bool maximized, kWindowType type, void *nativeHandle)
    {
        windowWidth = width;
        windowHeight = height;
        windowTitle = title;

        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            std::cout << "SDL could not initialize! SDL_Error: " << SDL_GetError() << std::endl;
            return false;
        }

        int flags = 0;
        if (useOpenGL)
            flags |= SDL_WINDOW_OPENGL;

        if (type == kWindowType::WINDOW_BORDERLESS)
        {
            flags |= SDL_WINDOW_BORDERLESS;
        }
        else if (type == kWindowType::WINDOW_FULLSCREEN)
        {
            flags |= SDL_WINDOW_FULLSCREEN;
        }

        if (maximized && type != kWindowType::WINDOW_FULLSCREEN)
        {
            flags |= SDL_WINDOW_RESIZABLE;
            flags |= SDL_WINDOW_MAXIMIZED;
        }

        if (nativeHandle != nullptr)
        {
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetPointerProperty(props, "native", nativeHandle);

            sdlWindow = SDL_CreateWindowWithProperties(props);
            SDL_DestroyProperties(props);
        }
        else
        {
            sdlWindow = SDL_CreateWindow(title.c_str(), width, height, flags);
        }

        if (sdlWindow == NULL)
        {
            std::cout << "Window could not be created! SDL_Error: " << SDL_GetError() << std::endl;
            return false;
        }

        return true;
    }

    void kWindow::destroy()
    {
        SDL_DestroyWindow(sdlWindow);
        SDL_Quit();
    }

    void kWindow::swap()
    {
        if (useOpenGL)
            SDL_GL_SwapWindow(getSdlWindow());
    }

    int kWindow::getWindowWidth()
    {
        return windowWidth;
    }

    int kWindow::getWindowHeight()
    {
        return windowHeight;
    }

    kString kWindow::getWindowTitle()
    {
        return windowTitle;
    }
	
	void kWindow::setWindowTitle(kString newTitle)
	{
		windowTitle = newTitle;
		SDL_SetWindowTitle(sdlWindow, newTitle.c_str());
	}

    bool kWindow::getRunning()
    {
        timer->tick();

        return running;
    }

    void kWindow::setRunning(bool newRunning)
    {
        running = newRunning;
    }

    kTimer *kWindow::getTimer()
    {
        return timer;
    }

    SDL_Window *kWindow::getSdlWindow()
    {
        return sdlWindow;
    }
}
