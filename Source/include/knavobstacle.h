/**
 * @file knavobstacle.h
 * @brief Dynamic navigation obstacle (cylinder) backed by dtTileCache.
 */

#ifndef KNAVOBSTACLE_H
#define KNAVOBSTACLE_H

#include "kexport.h"
#include "kdatatype.h"

namespace kemena
{
    // -------------------------------------------------------------------------
    // kNavObstacle
    // -------------------------------------------------------------------------

    /**
     * @brief A dynamic cylinder obstacle carved into the navmesh at runtime.
     *
     * Obstacles require a tiled navmesh (kNavBuildConfig::tileSize > 0).
     * Do not create directly — obtain via kNavManager::addObstacle().
     *
     * @code
     *   kNavObstacle* obs = navManager->addObstacle(pos, 0.8f, 2.0f);
     *   obs->setPosition(newPos);      // moves the obstacle
     *   navManager->removeObstacle(obs); // carves back in
     * @endcode
     */
    class KEMENA3D_API kNavObstacle
    {
    public:
        /** @brief Constructs an empty, uninitialised obstacle (no tile-cache binding). */
        kNavObstacle();

        /** @brief Destroys the obstacle, removing it from the tile cache if still valid. */
        ~kNavObstacle();

        // --- Accessors -------------------------------------------------------

        /**
         * @brief Returns the obstacle's current world-space centre position.
         * @return The cached position last set via init() or setPosition().
         */
        kVec3 getPosition() const;

        /**
         * @brief Returns the obstacle cylinder's radius.
         * @return The radius in world units.
         */
        float getRadius() const;

        /**
         * @brief Returns the obstacle cylinder's height.
         * @return The height in world units.
         */
        float getHeight() const;

        /**
         * @brief Moves the obstacle to a new world-space position.
         *
         * Internally removes the old dtObstacleRef and adds a new one.
         * Affected tiles are flagged for re-baking on the next
         * kNavManager::update() call. Has no effect if the obstacle is
         * not bound to a tile cache or is currently invalid.
         *
         * @param pos The new world-space centre position for the obstacle.
         */
        void setPosition(const kVec3 &pos);

        /** @brief Returns true if the obstacle is registered in the tile cache. */
        bool isValid() const;

        // --- Internal (used by kNavManager) ----------------------------------

        /**
         * @brief Registers the obstacle as a cylinder in the given tile cache.
         *
         * Stores the position/radius/height and calls dtTileCache::addObstacle().
         * Intended for internal use by kNavManager; callers should obtain
         * obstacles via kNavManager::addObstacle() instead.
         *
         * @param tileCache Opaque pointer to the owning dtTileCache.
         * @param position  World-space centre position of the cylinder.
         * @param radius     Cylinder radius in world units.
         * @param height     Cylinder height in world units.
         * @return true if the obstacle was successfully added to the tile cache.
         */
        bool init(void *tileCache, const kVec3 &position,
                  float radius, float height);

        /**
         * @brief Removes the obstacle from the tile cache and marks it invalid.
         *
         * Safe to call multiple times; does nothing if not currently valid.
         */
        void uninit();

        /**
         * @brief Returns the underlying Detour obstacle reference.
         * @return The opaque dtObstacleRef handle as an unsigned integer.
         */
        unsigned int getObstacleRef() const;

    protected:
    private:
        struct Impl;
        Impl *m_impl;
    };

} // namespace kemena

#endif // KNAVOBSTACLE_H
