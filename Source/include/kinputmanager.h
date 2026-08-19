/**
 * @file kinputmanager.h
 * @brief Named input / action-mapping manager.
 *
 * Layers a logical, platform-independent "action" API on top of the raw
 * keyboard / mouse / gamepad events exposed by kSystemEvent.  Instead of
 * hard-coding key or button checks all over game code, you bind one or more
 * physical inputs to a named action, then query that action everywhere.
 *
 * The same action can carry bindings from several devices at once, so a game
 * written once responds to a keyboard on PC, a gamepad on console, and a
 * touch/on-screen controller if a binding is later added — without touching
 * gameplay code.
 *
 * Typical usage:
 * @code
 *   kInputManager *input = createInputManager();
 *
 *   input->addAction("Jump");
 *   input->bindKey("Jump", K_KEY_SPACE);
 *   input->bindMouseButton("Jump", K_MOUSEBUTTON_LEFT);
 *   input->bindGamepadButton("Jump", K_GAMEPAD_BUTTON_SOUTH);
 *
 *   input->addAxis("MoveX");
 *   input->bindKeyAxis("MoveX", K_KEY_A, K_KEY_D);
 *   input->bindGamepadAxis("MoveX", K_GAMEPAD_AXIS_LEFTX);
 *
 *   // Each frame:
 *   input->update();
 *   if (input->getActionPressed("Jump")) jump();
 *   velocity.x = input->getAxis("MoveX") * moveSpeed;
 * @endcode
 */

#ifndef KINPUTMANAGER_H
#define KINPUTMANAGER_H

#include "kexport.h"
#include "kdatatype.h"

#include <string>
#include <unordered_map>

namespace kemena
{
    /**
     * @brief Maps named actions/axes to concrete keyboard, mouse, and gamepad
     *        bindings, and evaluates their current state each frame.
     *
     * Call update() once per frame (after window creation) before querying.
     * Gamepads are discovered automatically; connecting or disconnecting one at
     * runtime is handled transparently by update().
     */
    class KEMENA3D_API kInputManager
    {
    public:
        /** @brief Constructs the manager; call init() before use. */
        kInputManager();

        /** @brief Destroys the manager, calling shutdown() if still active. */
        ~kInputManager();

        // --- Lifecycle -------------------------------------------------------

        /**
         * @brief Initialises input polling and opens any connected gamepads.
         * @return true on success.
         */
        bool init();

        /** @brief Closes gamepads and shuts the gamepad subsystem down. */
        void shutdown();

        /**
         * @brief Refreshes device state and re-evaluates every action/axis.
         *
         * Must be called once per frame.  Device state is pumped here (without
         * draining the event queue), so it is safe to call alongside the normal
         * kSystemEvent polling loop.
         */
        void update();

        // --- Action management ----------------------------------------------

        /**
         * @brief Registers a named digital action (creating it if necessary).
         * @param action Unique action name (e.g. "Jump", "Fire").
         */
        void addAction(const kString &action);

        /** @brief Removes a named action and all of its bindings. */
        void removeAction(const kString &action);

        /** @brief Returns true if the named action exists. */
        bool hasAction(const kString &action) const;

        /** @brief Removes every registered action. */
        void clearActions();

        // --- Digital binding -------------------------------------------------

        /**
         * @brief Binds a keyboard key to an action.
         * @param action  Action name; created if it does not exist yet.
         * @param keyCode Key code (use the K_KEY_* constants).
         */
        void bindKey(const kString &action, unsigned int keyCode);

        /**
         * @brief Binds a mouse button to an action.
         * @param action      Action name; created if it does not exist yet.
         * @param mouseButton Mouse button (use the K_MOUSEBUTTON_* constants).
         */
        void bindMouseButton(const kString &action, unsigned int mouseButton);

        /**
         * @brief Binds a gamepad button to an action.
         * @param action       Action name; created if it does not exist yet.
         * @param gamepadButton Gamepad button (use the K_GAMEPAD_BUTTON_* constants).
         */
        void bindGamepadButton(const kString &action, unsigned int gamepadButton);

        // --- Axis management ------------------------------------------------

        /**
         * @brief Registers a named analog axis (creating it if necessary).
         * @param axis Unique axis name (e.g. "MoveX", "LookY").
         */
        void addAxis(const kString &axis);

        /** @brief Removes a named axis and all of its bindings. */
        void removeAxis(const kString &axis);

        /** @brief Returns true if the named axis exists. */
        bool hasAxis(const kString &axis) const;

        /**
         * @brief Binds two keyboard keys as the negative/positive ends of an axis.
         *
         * Holding @p negativeKey produces -1.0, @p positiveKey produces +1.0,
         * both together cancel to 0.0, and neither produces 0.0.
         *
         * @param axis        Axis name; created if it does not exist yet.
         * @param negativeKey Key code for the negative direction.
         * @param positiveKey Key code for the positive direction.
         */
        void bindKeyAxis(const kString &axis, unsigned int negativeKey, unsigned int positiveKey);

        /**
         * @brief Binds a gamepad stick/trigger axis to a named axis.
         *
         * The raw SDL axis value is normalised to [-1, 1].
         *
         * @param axis        Axis name; created if it does not exist yet.
         * @param gamepadAxis Gamepad axis (use the K_GAMEPAD_AXIS_* constants).
         * @param inverted    If true, the sign of the reported value is flipped.
         */
        void bindGamepadAxis(const kString &axis, unsigned int gamepadAxis, bool inverted = false);

        // --- Queries ---------------------------------------------------------

        /** @brief Returns true while the named action is held down. */
        bool getAction(const kString &action) const;

        /** @brief Returns true on the frame the named action transitions to pressed. */
        bool getActionPressed(const kString &action) const;

        /** @brief Returns true on the frame the named action transitions to released. */
        bool getActionReleased(const kString &action) const;

        /**
         * @brief Returns the current value of a named axis in the range [-1, 1].
         * @return 0.0f if the axis is unbound or unknown.
         */
        float getAxis(const kString &axis) const;

        /**
         * @brief Combines two named axes into a 2D vector.
         * @param axisX Axis used for the X component.
         * @param axisY Axis used for the Y component.
         */
        kVec2 getAxis2D(const kString &axisX, const kString &axisY) const;

        /** @brief Returns the number of currently open gamepads. */
        int getGamepadCount() const;

    protected:
    private:
        struct Impl;
        Impl *m_impl;
    };
}

#endif // KINPUTMANAGER_H
