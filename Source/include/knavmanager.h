/**
 * @file knavmanager.h
 * @brief High-level navigation manager: crowd agents, obstacles, tile cache updates.
 */

#ifndef KNAVMANAGER_H
#define KNAVMANAGER_H

#include "kexport.h"
#include "kdatatype.h"
#include "knavmesh.h"
#include "knavagent.h"
#include "knavobstacle.h"

#include <vector>

namespace kemena
{
    // -------------------------------------------------------------------------
    // kNavManager
    // -------------------------------------------------------------------------

    /**
     * @brief Owns a dtCrowd and a dtTileCache update loop for a single kNavMesh.
     *
     * Typical usage:
     * @code
     *   kNavMesh* mesh = new kNavMesh();
     *   mesh->bake(verts, tris, config);
     *
     *   kNavManager* nav = createNavManager(mesh);
     *
     *   kNavAgent* hero = nav->addAgent(startPos, {});
     *   hero->setTarget(goalPos);
     *
     *   kNavObstacle* barrel = nav->addObstacle(barrelPos, 0.4f, 1.2f);
     *
     *   // game loop:
     *   nav->update(deltaTime);
     *   kVec3 pos = hero->getPosition();
     * @endcode
     */
    class KEMENA3D_API kNavManager
    {
    public:
        /**
         * @brief Constructs a navigation manager for a baked navmesh.
         * @param mesh   A successfully baked kNavMesh. The manager holds a
         *               non-owning pointer — the mesh must outlive the manager.
         * @param maxAgents  Maximum simultaneous crowd agents (default 128).
         */
        kNavManager(kNavMesh *mesh, int maxAgents = 128);

        /** @brief Destroys the manager, shutting down the crowd and freeing agents/obstacles. */
        ~kNavManager();

        // --- Lifecycle -------------------------------------------------------

        /**
         * @brief Initialises the internal dtCrowd.
         * @return false if the mesh is not baked or dtCrowd allocation failed.
         */
        bool init();

        /** @brief Shuts down crowd and releases all agents / obstacles. */
        void shutdown();

        /**
         * @brief Steps the crowd simulation and rebuilds any dirty tiles.
         * @param deltaTime Frame time in seconds.
         */
        void update(float deltaTime);

        // --- Agents ----------------------------------------------------------

        /**
         * @brief Adds a new crowd agent at @p position.
         * @return Pointer owned by the manager, or nullptr if the crowd is full.
         */
        kNavAgent* addAgent(const kVec3 &position,
                            const kNavAgentConfig &config = {});

        /**
         * @brief Removes and destroys an agent.
         *
         * The pointer is invalid after this call.
         */
        void removeAgent(kNavAgent *agent);

        /** @brief All currently active agents. */
        const std::vector<kNavAgent*> &getAgents() const;

        // --- Obstacles -------------------------------------------------------

        /**
         * @brief Adds a dynamic cylinder obstacle.
         *
         * Requires the navmesh to have been built with tileSize > 0.
         * Returns nullptr for single-mesh builds.
         *
         * @param position  World-space centre (bottom) of the cylinder.
         * @param radius    Cylinder radius (m).
         * @param height    Cylinder height (m).
         */
        kNavObstacle* addObstacle(const kVec3 &position,
                                  float radius, float height);

        /**
         * @brief Removes and destroys a dynamic obstacle.
         *
         * The pointer is invalid after this call.
         */
        void removeObstacle(kNavObstacle *obstacle);

        /** @brief All currently registered obstacles. */
        const std::vector<kNavObstacle*> &getObstacles() const;

        // --- Query -----------------------------------------------------------

        /** @brief Returns the kNavMesh this manager was created with. */
        kNavMesh *getNavMesh() const;

    protected:
    private:
        struct Impl;
        Impl *m_impl;
    };

} // namespace kemena

#endif // KNAVMANAGER_H
