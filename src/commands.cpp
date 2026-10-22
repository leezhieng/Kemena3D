#include "commands.h"
#include "manager.h"
#include "terrain.h"

#include <kemena/kmesh.h>
#include <kemena/klight.h>
#include <kemena/kcamera.h>

#include <functional>
#include <algorithm>
#include <cstring>

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Insert `child` into `parent`'s children list immediately before
// `beforeSibling`. If `beforeSibling` is null, append. Used by Reparent /
// Instantiate / CreatePrefab undo paths to restore sibling order without
// needing index arithmetic.
static void insertChildBefore(kObject *parent, kObject *child, kObject *beforeSibling)
{
    if (!parent || !child) return;
    child->detachFromParent();
    if (!beforeSibling)
    {
        child->setParent(parent);
        return;
    }
    auto kids = parent->getChildren();
    std::vector<kObject *> tail;
    bool found = false;
    for (kObject *k : kids)
    {
        if (k == beforeSibling) found = true;
        if (found) tail.push_back(k);
    }
    for (kObject *k : tail) k->detachFromParent();
    child->setParent(parent);
    for (kObject *k : tail) k->setParent(parent);
}

// Locate an object by UUID anywhere in the active scene tree.
static kObject *findInTree(kObject *node, const kString &uuid)
{
    if (!node) return nullptr;
    if (node->getUuid() == uuid) return node;
    for (kObject *c : node->getChildren())
        if (kObject *f = findInTree(c, uuid)) return f;
    return nullptr;
}

static kObject *findInScene(kScene *scene, const kString &uuid)
{
    if (!scene) return nullptr;
    return findInTree(scene->getRootNode(), uuid);
}

// Recursive subtree teardown (free this object and every descendant).
// Used by InstantiateCommand / CreatePrefabCommand destructors when the
// command goes out of scope while the subtree is in the "detached" state.
static void freeSubtree(kObject *node)
{
    if (!node) return;
    auto kids = node->getChildren();
    for (kObject *c : kids) freeSubtree(c);
    delete node;
}

// ---------------------------------------------------------------------------
// UndoRedoManager
// ---------------------------------------------------------------------------

void UndoRedoManager::push(std::unique_ptr<ICommand> cmd)
{
    redoStack.clear();
    undoStack.push_back(std::move(cmd));
    if (undoStack.size() > MaxHistory)
        undoStack.pop_front();
}

void UndoRedoManager::undo()
{
    if (undoStack.empty()) return;
    auto &cmd = undoStack.back();
    cmd->undo();
    redoStack.push_back(std::move(cmd));
    undoStack.pop_back();
}

void UndoRedoManager::redo()
{
    if (redoStack.empty()) return;
    auto &cmd = redoStack.back();
    cmd->redo();
    undoStack.push_back(std::move(cmd));
    redoStack.pop_back();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void applyTransformStates(const std::vector<TransformState> &states, Manager *mgr)
{
    for (const auto &s : states)
    {
        kObject *obj = mgr->findObjectByUuid(s.uuid);
        if (!obj) continue;
        // Use force-setters so undo/redo can always restore transform state,
        // even when the object is marked as static (the static guard only
        // blocks runtime/gameplay modifications).
        obj->setPositionForced(s.pos);
        obj->setRotationForced(s.rot);
        obj->setScaleForced(s.scale);
    }
}

// ---------------------------------------------------------------------------
// TransformCommand
// ---------------------------------------------------------------------------

void TransformCommand::undo()
{
    applyTransformStates(before, manager);
}

void TransformCommand::redo()
{
    applyTransformStates(after, manager);
}

// ---------------------------------------------------------------------------
// DeleteCommand
// ---------------------------------------------------------------------------

DeleteCommand::~DeleteCommand()
{
    if (ownsObjects)
    {
        for (auto &info : deleted)
            delete info.object;
    }
}

void DeleteCommand::undo()
{
    // Re-add each object to its scene
    for (auto &info : deleted)
    {
        if (!info.scene || !info.object) continue;

        if (info.type == NODE_TYPE_LIGHT)
            info.scene->addLight(static_cast<kLight *>(info.object));
        else
            info.scene->addMesh(static_cast<kMesh *>(info.object), info.object->getUuid());
    }

    ownsObjects = false;

    // Restore selection
    manager->selectedObjects   = selectionBefore;
    manager->selectedObject    = selectedObjBefore;

    if (manager->panelHierarchy)
        manager->panelHierarchy->refreshList();
}

void DeleteCommand::redo()
{
    for (auto &info : deleted)
    {
        if (!info.scene || !info.object) continue;

        if (info.type == NODE_TYPE_LIGHT)
            info.scene->removeLight(static_cast<kLight *>(info.object));
        else
            info.scene->removeMesh(static_cast<kMesh *>(info.object));
    }

    ownsObjects = true;

    // Clear selection
    manager->selectedObjects.clear();
    manager->selectedObject = nullptr;

    if (manager->panelHierarchy)
        manager->panelHierarchy->refreshList();
}

// ---------------------------------------------------------------------------
// SelectCommand
// ---------------------------------------------------------------------------

void SelectCommand::undo()
{
    manager->selectedObjects = before;
    manager->selectedObject  = selectedObjBefore;
}

void SelectCommand::redo()
{
    manager->selectedObjects = after;
    manager->selectedObject  = selectedObjAfter;
}

// ---------------------------------------------------------------------------
// ReparentCommand
// ---------------------------------------------------------------------------

void ReparentCommand::undo()
{
    kObject *obj = findInScene(manager->getScene(), objUuid);
    if (!obj || !oldParent) return;
    insertChildBefore(oldParent, obj, oldNextSibling);
    // Restore full TRS so the object's world pose matches the pre-reparent
    // state exactly — the original reparent was world-transform-preserving.
    obj->setPosition(oldLocalPos);
    obj->setRotation(oldLocalRot);
    obj->setScale(oldLocalScale);
    if (manager->panelHierarchy) manager->panelHierarchy->refreshList();
}

void ReparentCommand::redo()
{
    kObject *obj = findInScene(manager->getScene(), objUuid);
    if (!obj || !newParent) return;
    insertChildBefore(newParent, obj, newNextSibling);
    obj->setPosition(newLocalPos);
    obj->setRotation(newLocalRot);
    obj->setScale(newLocalScale);
    if (manager->panelHierarchy) manager->panelHierarchy->refreshList();
}

// ---------------------------------------------------------------------------
// MaterialCommand
// ---------------------------------------------------------------------------

void MaterialCommand::undo()
{
    manager->restoreMaterialSubtree(before);
}

void MaterialCommand::redo()
{
    manager->restoreMaterialSubtree(after);
}

// ---------------------------------------------------------------------------
// InstantiateCommand
// ---------------------------------------------------------------------------

InstantiateCommand::~InstantiateCommand()
{
    // If the command is destroyed while the subtree is detached (i.e. the
    // user undid the spawn and the redo stack got cleared by some later
    // edit), the subtree is unreachable — free it so we don't leak.
    if (!attached && ownsSubtree)
        freeSubtree(root);
}

void InstantiateCommand::undo()
{
    if (!attached || !root) return;
    // Lights/cameras have side-vectors on scene/world. For the asset types we
    // currently spawn from drag-drop (mesh, prefab root, empty-with-audio)
    // detach is sufficient — we'd need richer logic if lights become drop
    // sources later.
    root->detachFromParent();
    attached    = false;
    ownsSubtree = true;
    if (manager->panelHierarchy) manager->panelHierarchy->refreshList();
}

void InstantiateCommand::redo()
{
    if (attached || !root || !parent) return;
    insertChildBefore(parent, root, nextSibling);
    attached    = true;
    ownsSubtree = false;
    if (manager->panelHierarchy) manager->panelHierarchy->refreshList();
}

// ---------------------------------------------------------------------------
// CreatePrefabCommand
// ---------------------------------------------------------------------------
// Multi-select Create Prefab does three things in sequence: create wrapper
// empty, reparent each selected object under it (and adjust local positions),
// stamp prefab_ref/template_uuid on every node in the new instance subtree.
// Undo reverses all of that, in reverse order.

void CreatePrefabCommand::undo()
{
    kScene *scene = manager->getScene();

    // 1) Clear the prefab linkage stamps.
    for (const auto &s : stamps)
    {
        kObject *obj = findInScene(scene, s.objUuid);
        if (!obj) continue;
        obj->setPrefabRef(s.oldPrefabRef);
        obj->setTemplateUuid(s.oldTemplateUuid);
    }

    // 2) Move each child back to its original parent + sibling order +
    //    local position. Walk in reverse so the most-recently-moved is
    //    restored first (matches the order they were placed under the empty).
    for (auto it = childMoves.rbegin(); it != childMoves.rend(); ++it)
    {
        kObject *obj = findInScene(scene, it->objUuid);
        if (!obj || !it->oldParent) continue;
        insertChildBefore(it->oldParent, obj, it->oldNextSibling);
        obj->setPosition(it->oldLocalPos);
        obj->setRotation(it->oldLocalRot);
        obj->setScale(it->oldLocalScale);
    }

    // 3) Tear down the wrapper empty if multi-select created one.
    if (createdEmpty)
    {
        createdEmpty->detachFromParent();
        // Don't delete: redo will re-attach the same instance. The command
        // owns it while detached.
    }

    if (manager->panelHierarchy) manager->panelHierarchy->refreshList();
}

void CreatePrefabCommand::redo()
{
    kScene *scene = manager->getScene();

    // 1) Re-attach the wrapper empty under its original parent (scene root).
    if (createdEmpty)
    {
        if (createdEmptyParent)
            createdEmpty->setParent(createdEmptyParent);
        createdEmpty->setPosition(createdEmptyPos);
    }

    // 2) Re-parent each child under the wrapper, in original move order, with
    //    the local-pos that was assigned at original-do time.
    for (const auto &m : childMoves)
    {
        kObject *obj = findInScene(scene, m.objUuid);
        if (!obj) continue;
        // We need access to the wrapper for the destination parent; multi-
        // select Create Prefab always uses createdEmpty as the destination.
        if (createdEmpty) insertChildBefore(createdEmpty, obj, /*append*/ nullptr);
        // Restore the exact local transform computed at do-time (the original
        // reparent preserved world transform).
        obj->setPosition(m.newLocalPos);
        obj->setRotation(m.newLocalRot);
        obj->setScale(m.newLocalScale);
    }

    // 3) Re-apply prefab linkage stamps.
    for (const auto &s : stamps)
    {
        kObject *obj = findInScene(scene, s.objUuid);
        if (!obj) continue;
        obj->setPrefabRef(s.newPrefabRef);
        obj->setTemplateUuid(s.newTemplateUuid);
    }

    if (manager->panelHierarchy) manager->panelHierarchy->refreshList();
}

// ---------------------------------------------------------------------------
// UnpackPrefabCommand
// ---------------------------------------------------------------------------

void UnpackPrefabCommand::undo()
{
    kScene *scene = manager->getScene();
    for (const auto &e : entries)
    {
        kObject *obj = findInScene(scene, e.objUuid);
        if (!obj) continue;
        obj->setPrefabRef(e.prefabRef);
        obj->setTemplateUuid(e.templateUuid);
    }
    if (manager->panelHierarchy) manager->panelHierarchy->refreshList();
}

void UnpackPrefabCommand::redo()
{
    kScene *scene = manager->getScene();
    for (const auto &e : entries)
    {
        kObject *obj = findInScene(scene, e.objUuid);
        if (!obj) continue;
        obj->setPrefabRef("");
        obj->setTemplateUuid("");
    }
    if (manager->panelHierarchy) manager->panelHierarchy->refreshList();
}

// ---------------------------------------------------------------------------
// TerrainEditCommand
// ---------------------------------------------------------------------------

// Copies a height sub-block (rows x cols) back into the tile's *base* height
// field.  Sculpting and height painting only ever modify the base; the generated
// modifier layers are composited back on afterwards by recomposeHeight().
static void applyHeightBlock(kTerrain *tile, int res,
                             int x0, int z0, int cols, int rows,
                             const std::vector<float> &src)
{
    float *dst = tile->getBaseHeightData();
    if (!dst) return;
    for (int z = 0; z < rows; ++z)
    {
        const float *s = src.data() + static_cast<size_t>(z) * cols;
        float *d = dst + static_cast<size_t>(z0 + z) * res + x0;
        std::copy(s, s + cols, d);
    }
}

// Copies a weight (splat) sub-block (rows x cols x kMaxTerrainLayers bytes)
// back into the tile's weight field.
static void applyWeightBlock(kTerrain *tile, int res,
                             int x0, int z0, int cols, int rows,
                             const std::vector<unsigned char> &src)
{
    unsigned char *dst = tile->getWeightData();
    if (!dst) return;
    const int L = kTerrain::kMaxTerrainLayers;
    for (int z = 0; z < rows; ++z)
    {
        const unsigned char *s = src.data() + static_cast<size_t>(z) * cols * L;
        unsigned char *d = dst + (static_cast<size_t>(z0 + z) * res + x0) * L;
        std::memcpy(d, s, static_cast<size_t>(cols) * L);
    }
}

// Writes one tile's stored sub-blocks back and refreshes its GPU data.
// `after` selects the "after" state (redo) instead of the "before" state (undo).
// `manager` (optional) is used to keep the editor selection valid across the
// mesh rebuild, which deletes and re-creates the tile's kMesh.
static void applyTileDiff(const TerrainEditCommand::TileDiff &d, bool after,
                          Manager *manager = nullptr)
{
    if (!d.tile) return;

    // Restore the nominal elevation together with the base field.
    if (d.hasTileHeight)
        d.tile->setTileHeight(after ? d.tileHeightAfter : d.tileHeightBefore);

    const bool touchesHeight = d.hasHeight || d.hasLayer;
    if (d.hasHeight)
        applyHeightBlock(d.tile, d.res, d.hx0, d.hz0, d.hxc, d.hzc,
                         after ? d.heightAfter : d.heightBefore);

    if (d.hasLayer)
    {
        // A generated modifier layer was created, re-generated, re-tuned or
        // removed. Restore the whole layer descriptor (params + offsets).
        std::vector<kTerrainHeightLayer> &layers = d.tile->getHeightLayers();
        const bool valid = after ? d.layerAfterValid : d.layerBeforeValid;

        if (valid)
        {
            const kTerrainHeightLayer &layer = after ? d.layerAfter : d.layerBefore;
            while (static_cast<int>(layers.size()) <= d.layerIndex)
                layers.push_back(kTerrainHeightLayer());
            if (d.layerIndex >= 0 && d.layerIndex < static_cast<int>(layers.size()))
                layers[d.layerIndex] = layer;
        }
        else if (d.layerIndex >= 0 && d.layerIndex < static_cast<int>(layers.size()))
        {
            layers.erase(layers.begin() + d.layerIndex);
        }

        // Inserting/removing a layer shifts every index above it. The panel's
        // tree buffers are keyed by index, so drop them or the next draw shows
        // the previous occupant's name and parameters.
        if (manager && manager->panelTerrain)
            manager->panelTerrain->invalidateLayerEditBuffers(d.tile);
    }

    if (d.hasWeight)
        applyWeightBlock(d.tile, d.res, d.wx0, d.wz0, d.wxc, d.wzc,
                         after ? d.weightAfter : d.weightBefore);

    // Fold the base + modifier layers into the rendered height field.
    if (touchesHeight)
        d.tile->recomposeHeight();

    if (d.tile->isLoaded())
    {
        if (touchesHeight)
        {
            kMesh *oldMesh = d.tile->getMesh();
            d.tile->rebuildMesh(true);
            kMesh *newMesh = d.tile->getMesh();
            if (manager && oldMesh && newMesh && oldMesh != newMesh)
            {
                // Re-point the raw selection at the rebuilt mesh (its UUID is
                // preserved) so it does not dangle.
                kObject *&activeSelection = manager->getActiveSelectedObject();
                if (activeSelection == oldMesh)
                    activeSelection = newMesh;
                if (manager->selectedObject == oldMesh)
                    manager->selectedObject = newMesh;
            }
        }
        if (d.hasWeight) d.tile->updateSplatTexture();
    }
}

void TerrainEditCommand::undo()
{
    for (const auto &d : diffs)
        applyTileDiff(d, false, manager);
    if (manager)
    {
        if (manager->getRenderer()) manager->getRenderer()->setOctreeDirty();
        manager->projectSaved = false;
    }
}

void TerrainEditCommand::redo()
{
    for (const auto &d : diffs)
        applyTileDiff(d, true, manager);
    if (manager)
    {
        if (manager->getRenderer()) manager->getRenderer()->setOctreeDirty();
        manager->projectSaved = false;
    }
}
