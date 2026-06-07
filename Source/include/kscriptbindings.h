/**
 * @file kscriptbindings.h
 * @brief Registers the engine's host API with an AngelScript engine.
 *
 * The binding layer exposes a minimal but useful surface to scripts:
 *  - @c kVec3            — 3-component float vector value type.
 *  - @c kObject          — scene node handle (transform, name, hierarchy).
 *  - @c getSelf()        — returns the kObject the running script is attached to.
 *  - @c getDeltaTime()   — frame delta time in seconds.
 *  - @c getFixedDeltaTime() — fixed-step delta time in seconds.
 *  - @c print() / log()  — console logging.
 */

#ifndef KSCRIPTBINDINGS_H
#define KSCRIPTBINDINGS_H

#include "kexport.h"
#include "kscriptmanager.h"

namespace kemena
{
    /**
     * @brief Registers all host types and functions on @p manager's engine.
     *
     * Call once, right after the kScriptManager is created and before any
     * script is compiled. Safe to call again only if the previous engine was
     * destroyed (AngelScript rejects duplicate registrations).
     *
     * @param manager The script manager whose engine receives the bindings.
     */
    KEMENA3D_API void registerScriptBindings(kScriptManager *manager);

    /**
     * @brief Routes the global host-API context (getSelf/getDeltaTime/etc.) to
     *        @p manager without re-registering anything on its engine.
     *
     * @c getSelf() and the time accessors resolve through a single global
     * pointer. Because every kWorld owns its own kScriptManager and calls
     * registerScriptBindings() in its constructor, the last world constructed
     * would otherwise "win" that global — so a secondary world (e.g. an editor
     * material/mesh preview) silently hijacks the running game's getSelf().
     * Call this at the top of each script-dispatch pass so the world about to
     * run scripts reclaims the context. Cheap: it only assigns the pointer.
     *
     * @param manager The script manager that should back the host API now.
     */
    KEMENA3D_API void setActiveScriptContext(kScriptManager *manager);
}

#endif // KSCRIPTBINDINGS_H
