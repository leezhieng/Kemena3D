#pragma once

#include "kemena/kemena.h"
#include <nlohmann/json.hpp>
#include <vector>

using namespace kemena;

// Forward-declared so this lightweight header can be included from the panel
// headers (which are themselves pulled in by manager.h) without creating a
// circular include through manager.h.
class Manager;

/**
 * @brief Snapshot of a single object's full state.
 *
 * Captured for every scene node when play (or isolated cinematic editing)
 * begins so the original editor scene can be fully restored afterwards. Stores
 * a complete JSON serialization of the object so non-transform properties
 * (camera FOV, light power, materials, etc.) are also reverted.
 */
struct ObjectTransformSnapshot
{
    kString        uuid;  ///< UUID of the object this snapshot belongs to.
    kObject       *object = nullptr; ///< Live object pointer at capture time.
    kObject       *parent = nullptr; ///< Parent at capture time (used to re-attach a destroyed node).
    kVec3          pos;   ///< Position at capture time.
    kQuat          rot;   ///< Rotation at capture time.
    kVec3          scale; ///< Scale at capture time.
    bool           active; ///< Active/enabled flag at capture time.
    nlohmann::json state; ///< Full serialized state for non-transform properties.
};

/**
 * @brief Capture/restore helpers for the editor scene state.
 *
 * Shared by the Game panel (rollback on Stop) and the Cinematic editor
 * (isolated edit mode) so both revert using identical behaviour. A snapshot
 * stores each node's transform, active flag, parent edge and full JSON
 * serialization so non-transform edits and destroyed nodes can be reverted.
 */
namespace SceneStateSnapshot
{
    /** @brief Recursively capture every scene node's state into @p out (clears it first). */
    void capture(Manager* manager, std::vector<ObjectTransformSnapshot>& out);

    /** @brief Restore every node in @p snapshots, then clear it. */
    void restore(Manager* manager, std::vector<ObjectTransformSnapshot>& snapshots);
}
