/**
 * @file knavmesh.h
 * @brief Navigation mesh baking and pathfinding.
 */

#ifndef KNAVMESH_H
#define KNAVMESH_H

#include "kexport.h"
#include "kdatatype.h"

#include <vector>

namespace kemena
{
    // -------------------------------------------------------------------------
    // Partition algorithm used during Recast region building
    // -------------------------------------------------------------------------

    enum class kNavPartitionType
    {
        Watershed, ///< Best quality; slower on complex geometry.
        Monotone,  ///< Fast; lower quality on open areas.
        Layers,    ///< Best for very flat/layered geometry.
    };

    // -------------------------------------------------------------------------
    // Build configuration
    // -------------------------------------------------------------------------

    /**
     * @brief All parameters that control the Recast bake pipeline.
     *
     * The defaults produce a navmesh suitable for a humanoid character
     * (≈2 m tall, 0.6 m radius) in a typical game world with 1-metre units.
     */
    struct kNavBuildConfig
    {
        // Voxel grid
        float cellSize             = 0.3f;  ///< XZ voxel size (m). Smaller = finer mesh.
        float cellHeight           = 0.2f;  ///< Y voxel size (m).

        // Agent dimensions
        float agentHeight          = 2.0f;  ///< Minimum clear headroom (m).
        float agentRadius          = 0.6f;  ///< Agent cylinder radius (m).
        float agentMaxClimb        = 0.9f;  ///< Maximum step-up height (m).
        float agentMaxSlope        = 45.0f; ///< Maximum walkable slope (degrees).

        // Region merging
        float regionMinSize        = 8.0f;  ///< Discard regions smaller than this (cells²).
        float regionMergeSize      = 20.0f; ///< Merge regions smaller than this (cells²).

        // Edge simplification
        float edgeMaxLen           = 12.0f; ///< Maximum edge length (m).
        float edgeMaxError         = 1.3f;  ///< Maximum edge deviation from original (m).
        int   maxVertsPerPoly      = 6;     ///< Maximum vertices per navmesh polygon (2–6).

        // Detail mesh
        float detailSampleDist     = 6.0f;
        float detailSampleMaxError = 1.0f;

        kNavPartitionType partition = kNavPartitionType::Watershed;

        /**
         * Tile size in voxel cells. Must be > 0 to enable dynamic obstacle support.
         * 32–64 is a typical range; 0 falls back to a single-mesh build (no obstacles).
         */
        float tileSize = 48.0f;
    };

    // -------------------------------------------------------------------------
    // Off-mesh connection (navmesh link)
    // -------------------------------------------------------------------------

    /**
     * @brief Defines a traversable shortcut between two navmesh points.
     *
     * Use this to represent ladders, jumps, teleporters, etc.
     * Links are baked into the navmesh and can be one- or two-directional.
     */
    struct kNavLink
    {
        kVec3 start;                ///< World-space start point.
        kVec3 end;                  ///< World-space end point.
        float radius        = 0.6f; ///< Connection snap radius.
        bool  bidirectional = true; ///< If false, only start→end is walkable.
    };

    // -------------------------------------------------------------------------
    // Editor-authored navigation surface descriptor
    // -------------------------------------------------------------------------

    /**
     * @brief Serialisable settings for a navigation surface attached to an
     *        object in the editor.
     *
     * Holds the bake parameters plus the geometry source: either every static
     * mesh in the scene, or only static meshes within an axis-aligned area box
     * centred on the owning object.
     */
    struct KEMENA3D_API kNavMeshDesc
    {
        kNavBuildConfig config;                       ///< Recast bake parameters.
        bool            useArea  = false;             ///< true = restrict to areaSize box.
        kVec3           areaSize = kVec3(20.0f, 10.0f, 20.0f); ///< Full extents of the area box.
    };

    // -------------------------------------------------------------------------
    // kNavMesh
    // -------------------------------------------------------------------------

    /**
     * @brief A baked navigation mesh used for pathfinding and obstacle simulation.
     *
     * Create one kNavMesh per scene. Call bake() with your world geometry, then
     * pass the mesh to kNavManager::init() to drive agents and obstacles.
     *
     * @code
     *   kNavMesh* nav = new kNavMesh();
     *
     *   // Collect world geometry
     *   std::vector<float> verts; // x,y,z,x,y,z,...
     *   std::vector<int>   tris;  // i0,i1,i2,...
     *   // ... fill from your world mesh ...
     *
     *   kNavBuildConfig cfg;
     *   cfg.agentRadius = 0.4f;
     *   nav->bake(verts, tris, cfg);
     *
     *   auto path = nav->findPath(start, end);
     * @endcode
     */
    class KEMENA3D_API kNavMesh
    {
    public:
        kNavMesh();
        ~kNavMesh();

        // --- Baking ----------------------------------------------------------

        /**
         * @brief Builds the navigation mesh from raw triangle geometry.
         * @param verts  Flat vertex buffer: x0,y0,z0, x1,y1,z1, ...
         * @param tris   Flat index buffer:  i0,i1,i2, i3,i4,i5, ...
         * @param config Voxelisation and agent parameters.
         * @param links  Optional off-mesh connections (ladders, jumps, …).
         * @return true on success.
         */
        bool bake(const std::vector<float>      &verts,
                  const std::vector<int>         &tris,
                  const kNavBuildConfig          &config = {},
                  const std::vector<kNavLink>    &links  = {});

        /** @brief Destroys the baked data and resets to an unbaked state. */
        void clear();

        /** @brief Returns true if the mesh has been successfully baked. */
        bool isBaked() const;

        // --- Pathfinding -----------------------------------------------------

        /**
         * @brief Finds a smoothed path along the navmesh from @p start to @p end.
         * @param maxPoints Maximum number of waypoints in the returned path.
         * @return World-space waypoints from start to end. Empty if unreachable.
         */
        std::vector<kVec3> findPath(const kVec3 &start, const kVec3 &end,
                                    int maxPoints = 256) const;

        /**
         * @brief Finds the nearest point on the navmesh surface to @p pos.
         * @param extents Half-extents of the AABB search region.
         * @return Snapped position, or @p pos unchanged if nothing is found.
         */
        kVec3 findNearestPoint(const kVec3 &pos,
                               const kVec3 &extents = kVec3(2.f, 4.f, 2.f)) const;

        /** @brief Returns true if @p pos is on (or very near) the navmesh. */
        bool isPointOnMesh(const kVec3 &pos,
                           const kVec3 &extents = kVec3(2.f, 4.f, 2.f)) const;

        /**
         * @brief Extracts the navmesh surface as world-space line segments for
         *        debug visualisation (wireframe).
         * @param outSegments Filled with pairs of points; each consecutive pair
         *                    (0,1), (2,3), … is one line. Cleared first.
         */
        void getDebugLines(std::vector<kVec3> &outSegments) const;

        // --- Internal (used by kNavManager) ----------------------------------

        /** @brief Returns the internal dtNavMesh pointer (opaque). */
        void *getNavMesh()      const;
        /** @brief Returns the internal dtNavMeshQuery pointer (opaque). */
        void *getNavMeshQuery() const;
        /** @brief Returns the internal dtTileCache pointer (null for non-tiled builds). */
        void *getTileCache()    const;

        struct Impl; ///< Opaque implementation (defined in knavmesh.cpp).

    protected:
    private:
        Impl *m_impl;
    };

} // namespace kemena

#endif // KNAVMESH_H
