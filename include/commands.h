#ifndef COMMANDS_H
#define COMMANDS_H

#include <functional>
#include <memory>
#include <deque>
#include <vector>

#include <kemena/kdatatype.h>
#include <kemena/kobject.h>
#include <kemena/kscene.h>
#include <kemena/kterrain.h>

using namespace kemena;

class Manager;  // forward declaration — full definition in manager.h

/**
 * @brief Pivot reference used when a transform gizmo acts on multiple objects.
 *
 * Determines the point around which a multi-object selection is rotated/scaled.
 */
enum class PivotMode
{
    Individual,   ///< Each object transforms around its own centre.
    Center,       ///< Transforms around the centroid of all selected objects.
    LastSelected  ///< Transforms around the last-selected object's pivot.
};

/**
 * @brief Per-object transform snapshot captured by TransformCommand.
 *
 * Records an object's identity and full local transform so a transform
 * operation can be replayed or reverted.
 */
struct TransformState
{
    kString uuid;  ///< UUID of the object this snapshot belongs to.
    kVec3   pos;   ///< Local position.
    kQuat   rot;   ///< Local rotation.
    kVec3   scale; ///< Local scale.
};

/**
 * @brief Abstract base interface for all undoable editor commands.
 *
 * Implements the command pattern used by UndoRedoManager: every concrete
 * command provides reversible undo() and redo() operations.
 */
struct ICommand
{
    virtual ~ICommand() = default;
    virtual void undo() = 0; ///< Revert the effect of this command.
    virtual void redo() = 0; ///< Re-apply the effect of this command.
};

/**
 * @brief Maintains the editor's undo and redo history.
 *
 * Owns two stacks of ICommand instances. Pushing a new command applies it as
 * the latest action, clears the redo stack, and trims the oldest entries once
 * the history exceeds MaxHistory.
 */
class UndoRedoManager
{
public:
    static constexpr size_t MaxHistory = 100; ///< Maximum number of retained commands.

    /**
     * @brief Push a freshly executed command onto the undo stack.
     *
     * Clears the redo stack and trims the oldest history beyond MaxHistory.
     * @param cmd Command to take ownership of and record.
     */
    void push(std::unique_ptr<ICommand> cmd);

    /// @brief Undo the most recent command, moving it to the redo stack.
    void undo();
    /// @brief Redo the most recently undone command, moving it back to the undo stack.
    void redo();

    /// @brief Whether there is at least one command available to undo.
    bool canUndo() const { return !undoStack.empty(); }
    /// @brief Whether there is at least one command available to redo.
    bool canRedo() const { return !redoStack.empty(); }
    /// @brief Discard the entire undo and redo history.
    void clear()         { undoStack.clear(); redoStack.clear(); }

private:
    std::deque<std::unique_ptr<ICommand>> undoStack; ///< Executed commands available to undo.
    std::deque<std::unique_ptr<ICommand>> redoStack; ///< Undone commands available to redo.
};

/**
 * @brief Generic command driven by a pair of undo/redo lambdas.
 *
 * Useful for simple property edits that can be captured as closures without
 * needing direct Manager access.
 */
struct PropertyCommand : ICommand
{
    std::function<void()> undoFn; ///< Callback invoked on undo().
    std::function<void()> redoFn; ///< Callback invoked on redo().

    /**
     * @brief Construct from an undo and a redo callback.
     * @param u Callback that reverts the change.
     * @param r Callback that (re-)applies the change.
     */
    PropertyCommand(std::function<void()> u, std::function<void()> r)
        : undoFn(std::move(u)), redoFn(std::move(r)) {}

    void undo() override { undoFn(); } ///< Invoke the undo callback.
    void redo() override { redoFn(); } ///< Invoke the redo callback.
};

/**
 * @brief Undoable transform edit from a gizmo or inspector drag.
 *
 * Stores before/after transform snapshots for one or more objects so a
 * multi-object transform can be reverted or replayed.
 */
struct TransformCommand : ICommand
{
    std::vector<TransformState> before; ///< Per-object transforms prior to the edit.
    std::vector<TransformState> after;  ///< Per-object transforms after the edit.
    Manager *manager;                   ///< Owning manager used to look up objects.

    /**
     * @brief Construct a transform command.
     * @param mgr Manager used to resolve objects by UUID.
     * @param b   Snapshots captured before the transform.
     * @param a   Snapshots captured after the transform.
     */
    TransformCommand(Manager *mgr,
                     std::vector<TransformState> b,
                     std::vector<TransformState> a)
        : manager(mgr), before(std::move(b)), after(std::move(a)) {}

    void undo() override; ///< Restore the "before" transforms.
    void redo() override; ///< Apply the "after" transforms.
};

/**
 * @brief Record of a single deleted object retained for undo.
 *
 * Holds the detached object together with the scene and node type needed to
 * re-attach it on undo.
 */
struct DeletedObjectInfo
{
    kObject  *object = nullptr;            ///< Detached object instance.
    kNodeType type   = NODE_TYPE_OBJECT;   ///< Node type the object was registered as.
    kScene   *scene  = nullptr;            ///< Scene the object belonged to.
};

/**
 * @brief Undoable deletion of one or more objects.
 *
 * Detaches the deleted objects and keeps them alive so a redo/undo cycle can
 * restore them, also remembering and restoring the prior selection state.
 */
struct DeleteCommand : ICommand
{
    std::vector<DeletedObjectInfo> deleted;            ///< Objects removed by this command.
    std::vector<kString>           selectionBefore;    ///< UUIDs selected before deletion.
    kObject                       *selectedObjBefore = nullptr; ///< Active object before deletion.
    Manager                       *manager;            ///< Owning manager.
    bool                           ownsObjects = true; ///< true → destructor frees detached objects.

    /**
     * @brief Construct a delete command.
     * @param mgr          Owning manager.
     * @param del          Objects being deleted, with restore metadata.
     * @param selBefore    UUIDs of the selection prior to deletion.
     * @param selObjBefore Active object prior to deletion.
     */
    DeleteCommand(Manager                       *mgr,
                  std::vector<DeletedObjectInfo> del,
                  std::vector<kString>           selBefore,
                  kObject                       *selObjBefore)
        : manager(mgr)
        , deleted(std::move(del))
        , selectionBefore(std::move(selBefore))
        , selectedObjBefore(selObjBefore) {}

    ~DeleteCommand() override; ///< Frees detached objects when ownsObjects is true.
    void undo() override;      ///< Re-attach the deleted objects and restore selection.
    void redo() override;      ///< Re-detach (delete) the objects again.
};

/**
 * @brief Undoable parent change or sibling reorder of a single object.
 *
 * The "next sibling" approach captures what comes after the moved object in
 * each parent's children list. When restoring state the object is inserted
 * before that sibling (or appended if the sibling is null), which sidesteps
 * fragile index math when other operations have shuffled siblings. Old/new
 * local transforms are stored so the object's world placement is preserved.
 */
struct ReparentCommand : ICommand
{
    Manager *manager; ///< Owning manager used to resolve objects.
    kString  objUuid; ///< UUID of the object being moved.

    kObject *oldParent       = nullptr; ///< Parent before the move.
    kObject *oldNextSibling  = nullptr; ///< Sibling following the object before the move.
    kVec3    oldLocalPos     = kVec3(0); ///< Local position before the move.
    kQuat    oldLocalRot     = kQuat(1.0f, 0.0f, 0.0f, 0.0f); ///< Local rotation before the move.
    kVec3    oldLocalScale   = kVec3(1.0f); ///< Local scale before the move.

    kObject *newParent       = nullptr; ///< Parent after the move.
    kObject *newNextSibling  = nullptr; ///< Sibling following the object after the move.
    kVec3    newLocalPos     = kVec3(0); ///< Local position after the move.
    kQuat    newLocalRot     = kQuat(1.0f, 0.0f, 0.0f, 0.0f); ///< Local rotation after the move.
    kVec3    newLocalScale   = kVec3(1.0f); ///< Local scale after the move.

    void undo() override; ///< Restore the object to its old parent/order/transform.
    void redo() override; ///< Re-apply the new parent/order/transform.
};

/**
 * @brief Per-node material snapshot used to revert/replay a material assignment.
 *
 * Captures one object's runtime material and its source asset UUID, keyed by
 * the object's UUID so it can be relocated in the scene during undo/redo.
 */
struct MaterialSnapshot
{
    kString    uuid;               ///< UUID of the object this snapshot belongs to.
    kMaterial *material = nullptr;  ///< Runtime material at snapshot time (owned by the asset manager).
    kString    materialUuid;        ///< Source .mat asset UUID at snapshot time.
};

/**
 * @brief Undoable material assignment over an object and its sub-meshes.
 *
 * Imported models are a root object plus import-derived sub-mesh children, so a
 * material drop must cover the whole subtree. Both before and after states are
 * captured as per-node snapshots so undo/redo restore each node exactly —
 * including the per-part materials a multi-mesh model started with.
 */
struct MaterialCommand : ICommand
{
    Manager                      *manager; ///< Owning manager used to resolve objects and restore materials.
    std::vector<MaterialSnapshot> before;  ///< Subtree material state before the assignment.
    std::vector<MaterialSnapshot> after;   ///< Subtree material state after the assignment.

    void undo() override; ///< Restore the "before" subtree materials.
    void redo() override; ///< Restore the "after" subtree materials.
};

/**
 * @brief Undoable spawn of a subtree into a scene.
 *
 * Mirrors DeleteCommand: when undone, the subtree is detached and held in
 * memory so a redo can re-attach. The destructor reclaims memory if the
 * command is dropped while the subtree is still detached.
 */
struct InstantiateCommand : ICommand
{
    Manager *manager;               ///< Owning manager.
    kObject *root         = nullptr; ///< Root of the instantiated subtree.
    kObject *parent       = nullptr; ///< Parent the subtree is attached under.
    kObject *nextSibling  = nullptr; ///< Sibling the root is inserted before (null → append).
    kScene  *scene        = nullptr; ///< Scene the subtree lives in.
    bool     attached     = true;   ///< Current attachment state.
    bool     ownsSubtree  = false;  ///< true while the subtree is detached (this command owns it).

    ~InstantiateCommand() override; ///< Frees the subtree if still detached.
    void undo() override;           ///< Detach the subtree from the scene.
    void redo() override;           ///< Re-attach the subtree to the scene.
};

/**
 * @brief Undoable "Create Prefab from selection" operation.
 *
 * Captures every in-scene structural change (multi-select wrapper empty,
 * reparenting, position offsets, prefab_ref/template_uuid stamps) so undo can
 * revert them. The .prefab file written to disk is NOT deleted on undo.
 */
struct CreatePrefabCommand : ICommand
{
    /**
     * @brief Records a child object's reparent and local-transform change.
     *
     * Used to revert the moves performed when wrapping a multi-select into the
     * generated prefab root.
     */
    struct ChildMove {
        kString  objUuid;        ///< UUID of the moved child.
        kObject *oldParent       = nullptr; ///< Parent before the move.
        kObject *oldNextSibling  = nullptr; ///< Following sibling before the move.
        kVec3    oldLocalPos     = kVec3(0); ///< Local position before the move.
        kQuat    oldLocalRot     = kQuat(1.0f, 0.0f, 0.0f, 0.0f); ///< Local rotation before the move.
        kVec3    oldLocalScale   = kVec3(1.0f); ///< Local scale before the move.
        kVec3    newLocalPos     = kVec3(0); ///< Local position after the move.
        kQuat    newLocalRot     = kQuat(1.0f, 0.0f, 0.0f, 0.0f); ///< Local rotation after the move.
        kVec3    newLocalScale   = kVec3(1.0f); ///< Local scale after the move.
    };
    /**
     * @brief Records the prefab reference / template UUID stamp on one object.
     *
     * Stores prior and new values so the prefab linkage can be removed on undo.
     */
    struct StampChange {
        kString objUuid;         ///< UUID of the stamped object.
        kString oldPrefabRef;    ///< prefab_ref before stamping.
        kString oldTemplateUuid; ///< template_uuid before stamping.
        kString newPrefabRef;    ///< prefab_ref after stamping.
        kString newTemplateUuid; ///< template_uuid after stamping.
    };

    Manager *manager = nullptr; ///< Owning manager.

    /// @brief Wrapper empty created for multi-select; nullptr for single-select.
    kObject *createdEmpty       = nullptr;
    kObject *createdEmptyParent = nullptr; ///< Parent of the wrapper (scene root unless overridden).
    kVec3    createdEmptyPos    = kVec3(0); ///< World position assigned to the wrapper empty.

    std::vector<ChildMove>   childMoves;   ///< Child reparents performed (empty for single-select).
    std::vector<StampChange> stamps;       ///< Prefab stamps applied (used in both modes).

    void undo() override; ///< Revert wrapper creation, reparents, and stamps.
    void redo() override; ///< Re-apply wrapper creation, reparents, and stamps.
};

/**
 * @brief Undoable strip of prefab_ref / template_uuid from objects.
 *
 * Records each object's prior prefab linkage so unpacking can be reversed.
 */
struct UnpackPrefabCommand : ICommand
{
    /**
     * @brief Saved prefab linkage for one unpacked object.
     */
    struct Entry {
        kString objUuid;      ///< UUID of the unpacked object.
        kString prefabRef;    ///< prefab_ref value removed by the unpack.
        kString templateUuid; ///< template_uuid value removed by the unpack.
    };

    Manager *manager = nullptr;    ///< Owning manager.
    std::vector<Entry> entries;    ///< Per-object linkage to restore on undo.

    void undo() override; ///< Restore the prefab_ref / template_uuid stamps.
    void redo() override; ///< Strip the prefab_ref / template_uuid stamps.
};

/**
 * @brief Undoable edit to a terrain tile's height field and/or weight (splat) map.
 *
 * Stores only the rectangular sub-region of a tile's buffers that the edit
 * actually changed, so sculpt/paint strokes and erosion passes stay cheap in the
 * undo history even for high-resolution tiles. Undo writes the stored "before"
 * sub-blocks back, redo writes the "after" sub-blocks; both then refresh the
 * tile's GPU mesh / splat texture.
 */
struct TerrainEditCommand : ICommand
{
    /// A single tile's changed sub-region(s). One stroke may span several tiles,
    /// so the command holds one entry per affected tile and undoes them together.
    struct TileDiff
    {
        kTerrain *tile = nullptr;   ///< Tile that was edited.
        int       res  = 0;         ///< Height resolution of the tile (row stride = res).

        bool hasHeight = false;     ///< Whether a height-field diff is present.
        int  hx0 = 0, hz0 = 0;      ///< Height diff rectangle origin.
        int  hxc = 0, hzc = 0;      ///< Height diff rectangle size (columns, rows).
        std::vector<float> heightBefore; ///< Height sub-block prior to the edit (hxc*hzc).
        std::vector<float> heightAfter;  ///< Height sub-block after the edit (hxc*hzc).

        bool hasWeight = false;     ///< Whether a weight-map diff is present.
        int  wx0 = 0, wz0 = 0;      ///< Weight diff rectangle origin.
        int  wxc = 0, wzc = 0;      ///< Weight diff rectangle size (columns, rows).
        std::vector<unsigned char> weightBefore; ///< Weight sub-block before (wxc*wzc*16).
        std::vector<unsigned char> weightAfter;  ///< Weight sub-block after (wxc*wzc*16).
    };

    std::vector<TileDiff> diffs; ///< Per-tile changes making up this command.
    Manager *manager = nullptr;  ///< Owning manager (renderer refresh + dirty flag).

    void undo() override; ///< Restore the "before" sub-blocks of every tile.
    void redo() override; ///< Restore the "after" sub-blocks of every tile.
};

/**
 * @brief Records a selection change so it can be undone and redone.
 *
 * Stores the selected UUID set and the active object both before and after the
 * change.
 */
struct SelectCommand : ICommand
{
    std::vector<kString> before; ///< Selected UUIDs before the change.
    std::vector<kString> after;  ///< Selected UUIDs after the change.
    kObject             *selectedObjBefore = nullptr; ///< Active object before the change.
    kObject             *selectedObjAfter  = nullptr; ///< Active object after the change.
    Manager             *manager;                     ///< Owning manager.

    /**
     * @brief Construct a selection command.
     * @param mgr  Owning manager.
     * @param b    Selected UUIDs before the change.
     * @param selB Active object before the change.
     * @param a    Selected UUIDs after the change.
     * @param selA Active object after the change.
     */
    SelectCommand(Manager            *mgr,
                  std::vector<kString> b, kObject *selB,
                  std::vector<kString> a, kObject *selA)
        : manager(mgr)
        , before(std::move(b)), selectedObjBefore(selB)
        , after(std::move(a)),  selectedObjAfter(selA) {}

    void undo() override; ///< Restore the "before" selection.
    void redo() override; ///< Restore the "after" selection.
};

#endif // COMMANDS_H
