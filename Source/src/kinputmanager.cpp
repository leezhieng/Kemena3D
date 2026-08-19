#include "kinputmanager.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace kemena
{
    // ---------------------------------------------------------------------
    // Internal binding description
    // ---------------------------------------------------------------------
    enum kBindingType
    {
        BINDING_KEY,            ///< Keyboard key (K_KEY_*).
        BINDING_MOUSE_BUTTON,   ///< Mouse button (K_MOUSEBUTTON_*).
        BINDING_GAMEPAD_BUTTON, ///< Gamepad button (K_GAMEPAD_BUTTON_*).
    };

    struct kBinding
    {
        kBindingType type;
        unsigned int code;
    };

    enum kAxisBindingType
    {
        AXIS_BINDING_KEY,          ///< Two keys acting as -1 / +1.
        AXIS_BINDING_GAMEPAD_AXIS, ///< Gamepad stick/trigger (K_GAMEPAD_AXIS_*).
    };

    struct kAxisBinding
    {
        kAxisBindingType type;
        unsigned int codeA;  ///< Negative key, or the gamepad axis id.
        unsigned int codeB;  ///< Positive key (unused for gamepad axes).
        bool inverted;
    };

    struct kActionState
    {
        bool current = false;
        bool previous = false;
        std::vector<kBinding> bindings;
    };

    struct kAxisState
    {
        float value = 0.0f;
        std::vector<kAxisBinding> bindings;
    };

    // ---------------------------------------------------------------------
    // Pimpl
    // ---------------------------------------------------------------------
    struct kInputManager::Impl
    {
        bool initialized = false;
        bool gamepadSupported = false;

        std::unordered_map<kString, kActionState> actions;
        std::unordered_map<kString, kAxisState> axes;

        // Open gamepads, keyed by their SDL instance id.
        std::vector<std::pair<SDL_JoystickID, SDL_Gamepad *>> gamepads;

        // ---- Raw device queries -----------------------------------------

        bool isKeyDown(unsigned int keyCode)
        {
            int numKeys = 0;
            const bool *state = SDL_GetKeyboardState(&numKeys);
            if (!state)
                return false;

            SDL_Scancode sc = SDL_GetScancodeFromKey((SDL_Keycode)keyCode, nullptr);
            if (sc < 0 || sc >= numKeys)
                return false;

            return state[sc];
        }

        bool isMouseButtonDown(unsigned int mouseButton)
        {
            SDL_MouseButtonFlags flags = SDL_GetMouseState(nullptr, nullptr);
            return (flags & SDL_BUTTON_MASK(mouseButton)) != 0;
        }

        bool isGamepadButtonDown(unsigned int gamepadButton)
        {
            for (auto &entry : gamepads)
            {
                SDL_Gamepad *gp = entry.second;
                if (gp && SDL_GetGamepadButton(gp, (SDL_GamepadButton)gamepadButton))
                    return true;
            }
            return false;
        }

        float getGamepadAxisValue(unsigned int gamepadAxis, bool inverted)
        {
            float best = 0.0f;
            for (auto &entry : gamepads)
            {
                SDL_Gamepad *gp = entry.second;
                if (!gp)
                    continue;

                float v = (float)SDL_GetGamepadAxis(gp, (SDL_GamepadAxis)gamepadAxis) / 32767.0f;
                if (std::fabs(v) > std::fabs(best))
                    best = v;
            }
            return inverted ? -best : best;
        }

        // ---- Gamepad hotplug ---------------------------------------------

        void refreshGamepads()
        {
            if (!gamepadSupported)
                return;

            int count = 0;
            SDL_JoystickID *ids = SDL_GetGamepads(&count);

            if (!ids || count <= 0)
            {
                if (ids)
                    SDL_free(ids);

                for (auto &entry : gamepads)
                {
                    if (entry.second)
                        SDL_CloseGamepad(entry.second);
                }
                gamepads.clear();
                return;
            }

            // Close gamepads that are no longer connected.
            for (auto it = gamepads.begin(); it != gamepads.end();)
            {
                bool stillConnected = false;
                for (int i = 0; i < count; ++i)
                {
                    if (ids[i] == it->first)
                    {
                        stillConnected = true;
                        break;
                    }
                }

                if (!stillConnected)
                {
                    if (it->second)
                        SDL_CloseGamepad(it->second);
                    it = gamepads.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // Open gamepads that have just appeared.
            for (int i = 0; i < count; ++i)
            {
                bool alreadyOpen = false;
                for (auto &entry : gamepads)
                {
                    if (entry.first == ids[i])
                    {
                        alreadyOpen = true;
                        break;
                    }
                }

                if (!alreadyOpen)
                {
                    SDL_Gamepad *gp = SDL_OpenGamepad(ids[i]);
                    if (gp)
                        gamepads.push_back(std::make_pair(ids[i], gp));
                }
            }

            SDL_free(ids);
        }
    };

    // ---------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------
    kInputManager::kInputManager()
    {
        m_impl = new Impl();
    }

    kInputManager::~kInputManager()
    {
        shutdown();
        delete m_impl;
        m_impl = nullptr;
    }

    bool kInputManager::init()
    {
        if (m_impl->initialized)
            return true;

        // Gamepad is optional; keyboard/mouse polling works regardless.
        m_impl->gamepadSupported = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        m_impl->initialized = true;

        if (m_impl->gamepadSupported)
            m_impl->refreshGamepads();

        return true;
    }

    void kInputManager::shutdown()
    {
        if (!m_impl->initialized)
            return;

        if (m_impl->gamepadSupported)
        {
            for (auto &entry : m_impl->gamepads)
            {
                if (entry.second)
                    SDL_CloseGamepad(entry.second);
            }
            m_impl->gamepads.clear();
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            m_impl->gamepadSupported = false;
        }

        m_impl->initialized = false;
    }

    void kInputManager::update()
    {
        if (!m_impl->initialized)
            return;

        // Refresh cached input state without draining the SDL event queue, so
        // the regular kSystemEvent loop keeps seeing the same events.
        SDL_PumpEvents();

        if (m_impl->gamepadSupported)
            m_impl->refreshGamepads();

        // Digital actions.
        for (auto &entry : m_impl->actions)
        {
            kActionState &state = entry.second;
            state.previous = state.current;

            bool down = false;
            for (const kBinding &binding : state.bindings)
            {
                switch (binding.type)
                {
                case BINDING_KEY:
                    down = m_impl->isKeyDown(binding.code);
                    break;
                case BINDING_MOUSE_BUTTON:
                    down = m_impl->isMouseButtonDown(binding.code);
                    break;
                case BINDING_GAMEPAD_BUTTON:
                    down = m_impl->isGamepadButtonDown(binding.code);
                    break;
                default:
                    down = false;
                    break;
                }

                if (down)
                    break;
            }

            state.current = down;
        }

        // Analog axes.
        for (auto &entry : m_impl->axes)
        {
            kAxisState &state = entry.second;

            float value = 0.0f;
            for (const kAxisBinding &binding : state.bindings)
            {
                if (binding.type == AXIS_BINDING_KEY)
                {
                    float negative = m_impl->isKeyDown(binding.codeA) ? -1.0f : 0.0f;
                    float positive = m_impl->isKeyDown(binding.codeB) ? 1.0f : 0.0f;
                    value += negative + positive;
                }
                else if (binding.type == AXIS_BINDING_GAMEPAD_AXIS)
                {
                    value += m_impl->getGamepadAxisValue(binding.codeA, binding.inverted);
                }
            }

            state.value = glm::clamp(value, -1.0f, 1.0f);
        }
    }

    // ---- Actions ----------------------------------------------------------

    void kInputManager::addAction(const kString &action)
    {
        m_impl->actions[action];
    }

    void kInputManager::removeAction(const kString &action)
    {
        m_impl->actions.erase(action);
    }

    bool kInputManager::hasAction(const kString &action) const
    {
        return m_impl->actions.find(action) != m_impl->actions.end();
    }

    void kInputManager::clearActions()
    {
        m_impl->actions.clear();
    }

    void kInputManager::bindKey(const kString &action, unsigned int keyCode)
    {
        m_impl->actions[action].bindings.push_back({BINDING_KEY, keyCode});
    }

    void kInputManager::bindMouseButton(const kString &action, unsigned int mouseButton)
    {
        m_impl->actions[action].bindings.push_back({BINDING_MOUSE_BUTTON, mouseButton});
    }

    void kInputManager::bindGamepadButton(const kString &action, unsigned int gamepadButton)
    {
        m_impl->actions[action].bindings.push_back({BINDING_GAMEPAD_BUTTON, gamepadButton});
    }

    // ---- Axes -------------------------------------------------------------

    void kInputManager::addAxis(const kString &axis)
    {
        m_impl->axes[axis];
    }

    void kInputManager::removeAxis(const kString &axis)
    {
        m_impl->axes.erase(axis);
    }

    bool kInputManager::hasAxis(const kString &axis) const
    {
        return m_impl->axes.find(axis) != m_impl->axes.end();
    }

    void kInputManager::bindKeyAxis(const kString &axis, unsigned int negativeKey, unsigned int positiveKey)
    {
        kAxisBinding binding;
        binding.type = AXIS_BINDING_KEY;
        binding.codeA = negativeKey;
        binding.codeB = positiveKey;
        binding.inverted = false;
        m_impl->axes[axis].bindings.push_back(binding);
    }

    void kInputManager::bindGamepadAxis(const kString &axis, unsigned int gamepadAxis, bool inverted)
    {
        kAxisBinding binding;
        binding.type = AXIS_BINDING_GAMEPAD_AXIS;
        binding.codeA = gamepadAxis;
        binding.codeB = 0;
        binding.inverted = inverted;
        m_impl->axes[axis].bindings.push_back(binding);
    }

    // ---- Queries ----------------------------------------------------------

    bool kInputManager::getAction(const kString &action) const
    {
        auto it = m_impl->actions.find(action);
        return it != m_impl->actions.end() && it->second.current;
    }

    bool kInputManager::getActionPressed(const kString &action) const
    {
        auto it = m_impl->actions.find(action);
        return it != m_impl->actions.end() && it->second.current && !it->second.previous;
    }

    bool kInputManager::getActionReleased(const kString &action) const
    {
        auto it = m_impl->actions.find(action);
        return it != m_impl->actions.end() && !it->second.current && it->second.previous;
    }

    float kInputManager::getAxis(const kString &axis) const
    {
        auto it = m_impl->axes.find(axis);
        return it != m_impl->axes.end() ? it->second.value : 0.0f;
    }

    kVec2 kInputManager::getAxis2D(const kString &axisX, const kString &axisY) const
    {
        return kVec2(getAxis(axisX), getAxis(axisY));
    }

    int kInputManager::getGamepadCount() const
    {
        return (int)m_impl->gamepads.size();
    }
}
