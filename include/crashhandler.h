/**
 * @file crashhandler.h
 * @brief Last-resort crash reporter for the editor.
 *
 * The Release build runs on the Windows GUI subsystem (no console), so any
 * printf/cerr diagnostics are invisible and a fault just closes the window
 * silently. installCrashHandler() registers an unhandled-exception filter that,
 * on an access violation (or any unhandled SEH exception), writes a symbolised
 * call stack to "Kemena3D_crash.log" next to the executable and shows a message
 * box pointing at it. Works in both Debug and Release builds.
 */

#ifndef KEMENA3D_CRASHHANDLER_H
#define KEMENA3D_CRASHHANDLER_H

namespace kemena
{
    /**
     * @brief Install the process-wide unhandled-exception filter.
     *
     * Call once, as early as possible in main(). No-op on non-Windows builds.
     */
    void installCrashHandler();
}

#endif // KEMENA3D_CRASHHANDLER_H
