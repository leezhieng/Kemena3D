/**
 * @file koctree.h
 * @brief Frustum class and loose octree for scene-level mesh culling.
 */

#ifndef KOCTREE_H
#define KOCTREE_H

#include "kexport.h"
#include "kdatatype.h"

#include <vector>
#include <memory>
#include <unordered_set>
#include <functional>

namespace kemena
{
    class kMesh;
    class kScene;
    class kObject;

    // -----------------------------------------------------------------------

    /**
     * @brief Result of testing a volume against a view frustum.
     */
    enum class kFrustumTestResult
    {
        Outside,      ///< Volume lies entirely outside the frustum.
        Intersecting, ///< Volume straddles one or more frustum planes.
        Inside        ///< Volume lies entirely inside all six planes.
    };

    /**
     * @brief View frustum defined by six planes extracted from a view-projection matrix.
     *
     * Call extractFromMatrix() with (projection * view) each frame before querying.
     */
    class KEMENA3D_API kFrustum
    {
    public:
        /**
         * @brief Extract the six frustum planes from a combined view-projection matrix.
         *
         * Uses the Gribb-Hartmann method (row combination of the VP matrix).
         * The planes are stored in the form  n·x + d = 0  where positive values
         * are inside the frustum.
         *
         * @param viewProjection  projection * view matrix.
         */
        void extractFromMatrix(const kMat4 &viewProjection);

        /**
         * @brief Test an AABB against the frustum.
         * @return Outside      — AABB is fully outside at least one plane.
         *         Intersecting — AABB crosses one or more planes.
         *         Inside       — AABB is fully inside all six planes.
         */
        kFrustumTestResult testAABB(const kAABB &aabb) const;

        /** @brief Quick reject: returns false if the AABB is fully outside. */
        bool intersectsAABB(const kAABB &aabb) const;

    private:
        kVec4 planes[6]; // (nx, ny, nz, d) — positive half-space is inside
    };

    // -----------------------------------------------------------------------

    /**
     * @brief Loose octree that spatially indexes scene meshes for frustum culling.
     *
     * Usage per frame:
     * @code
     *   octree.build(scene);                        // rebuild from scene
     *   kFrustum frustum;
     *   frustum.extractFromMatrix(proj * view);
     *   auto visible = octree.queryVisible(frustum); // cull
     *   for (kMesh *m : visible) renderMesh(m);
     * @endcode
     */
    class KEMENA3D_API kOctree
    {
    public:
        /**
         * @brief Construct an empty octree with the given subdivision limits.
         * @param maxDepth          Maximum recursion depth (default 6).
         * @param maxObjectsPerNode Meshes per leaf before subdivision (default 8).
         */
        kOctree(int maxDepth = 6, int maxObjectsPerNode = 8);

        /** @brief Destroy the octree and release all nodes. */
        ~kOctree();

        /**
         * @brief Rebuild the tree from all loaded meshes in the scene.
         *
         * Computes world-space AABBs, derives world bounds, then inserts every
         * mesh into the appropriate octree node.  Call once per frame (or
         * whenever the scene changes).
         */
        void build(kScene *scene);

        /**
         * @brief Return all meshes whose world AABB intersects the frustum.
         *
         * Traverses the tree top-down; entire subtrees whose bounds are outside
         * the frustum are skipped, and subtrees fully inside are collected
         * without further plane tests.
         */
        std::vector<kMesh*> queryVisible(const kFrustum &frustum) const;

        /** @brief Discard all nodes and mesh references. */
        void clear();

        /** @brief Get the total number of nodes currently in the tree. */
        int getNodeCount()  const;

        /** @brief Get the total number of meshes indexed by the tree. */
        int getMeshCount()  const { return totalMeshes; }

        /**
         * @brief Visit every node in the tree top-down.
         *
         * The callback receives the node's world-space AABB, its depth (root = 0),
         * and whether it is a leaf node.  Useful for debug visualization.
         *
         * @param visitor  Callable: void(const kAABB &bounds, int depth, bool isLeaf)
         */
        void traverse(const std::function<void(const kAABB &, int, bool)> &visitor) const;

    private:
        /**
         * @brief A single octree node holding a bounded region of space.
         *
         * A node is either a leaf (storing meshes directly) or an internal node
         * with up to eight child octants.
         */
        struct Node
        {
            kAABB bounds;                         ///< World-space bounds of this node.
            std::vector<kMesh*> meshes;           ///< Meshes stored directly in this node.
            std::unique_ptr<Node> children[8];    ///< Eight child octants (null until subdivided).
            bool leaf = true;                     ///< True while the node has no children.

            /** @brief Split this leaf into eight child octants. */
            void subdivide();

            /**
             * @brief Insert a mesh into this node or an appropriate descendant.
             * @param mesh       Mesh to insert.
             * @param meshBounds World-space AABB of the mesh.
             * @param depth      Current depth of this node (root = 0).
             * @param maxDepth   Maximum allowed recursion depth.
             * @param maxObj     Mesh count that triggers subdivision.
             */
            void insert(kMesh *mesh, const kAABB &meshBounds, int depth,
                        int maxDepth, int maxObj);

            /**
             * @brief Collect every mesh in this node and all descendants.
             * @param out Destination vector that receives the meshes.
             */
            void collectAll(std::vector<kMesh*> &out) const;

            /**
             * @brief Recursively gather meshes whose bounds intersect the frustum.
             * @param frustum View frustum to cull against.
             * @param out     Destination vector that receives the visible meshes.
             */
            void query(const kFrustum &frustum, std::vector<kMesh*> &out) const;

            /**
             * @brief Recursively visit this node and its descendants.
             * @param visitor Callable: void(const kAABB &bounds, int depth, bool isLeaf).
             * @param depth   Depth of this node passed to the visitor.
             */
            void traverse(const std::function<void(const kAABB &, int, bool)> &visitor,
                          int depth) const;

            /** @brief Count this node plus all descendant nodes. */
            int  nodeCount() const;
        };

        std::unique_ptr<Node> root; ///< Root node of the tree (null when empty).
        int maxDepth;               ///< Maximum recursion depth for subdivision.
        int maxObjectsPerNode;      ///< Mesh count per leaf that triggers subdivision.
        int totalMeshes = 0;        ///< Total number of meshes indexed by the tree.

        /**
         * @brief Recursively gather all meshes attached to an object hierarchy.
         * @param node Root object to walk.
         * @param out  Destination vector that receives the meshes.
         */
        static void collectMeshes(kObject *node, std::vector<kMesh*> &out);
    };
}

#endif // KOCTREE_H
