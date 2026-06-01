/**
 * @file knavagent.h
 * @brief Navigation crowd agent driven by Detour Crowd.
 */

#ifndef KNAVAGENT_H
#define KNAVAGENT_H

#include "kexport.h"
#include "kdatatype.h"

namespace kemena
{
    // -------------------------------------------------------------------------
    // Agent configuration
    // -------------------------------------------------------------------------

    /**
     * @brief Per-agent parameters passed to dtCrowd on creation.
     *
     * Most values can be updated at runtime via kNavAgent setters.
     */
    struct kNavAgentConfig
    {
        float radius            = 0.6f;   ///< Agent cylinder radius (m).
        float height            = 2.0f;   ///< Agent cylinder height (m).
        float maxAcceleration   = 8.0f;   ///< Maximum acceleration (m/s²).
        float maxSpeed          = 3.5f;   ///< Maximum speed (m/s).

        /** How closely to follow the path before considering a waypoint reached. */
        float pathOptRange      = 30.0f;

        /** Separation weight when agents are adjacent. 0 = no separation. */
        float separationWeight  = 2.0f;

        /** If true the agent will be pushed away from neighbours. */
        bool  anticipateTurns   = true;
        bool  optimizeVis       = true;   ///< Optimise path visibility.
        bool  optimizeTopo      = true;   ///< Optimise path topology.
        bool  obstacleAvoidance = true;   ///< Use local steering avoidance.
        bool  separation        = false;  ///< Enable crowd separation.
    };

    // -------------------------------------------------------------------------
    // kNavAgent
    // -------------------------------------------------------------------------

    /**
     * @brief A single crowd agent managed by kNavManager.
     *
     * Do not create directly — obtain via kNavManager::addAgent().
     * The agent is driven by Detour Crowd each frame and moves toward its
     * current target.
     *
     * @code
     *   kNavAgent* agent = navManager->addAgent(spawnPos, config);
     *   agent->setTarget(goalPos);
     *   // each frame:
     *   agent->getPosition(); // current smooth position
     * @endcode
     */
    class KEMENA3D_API kNavAgent
    {
    public:
        /** @brief Constructs an uninitialised agent; call init() before use. */
        kNavAgent();

        /** @brief Destroys the agent, releasing internal crowd resources. */
        ~kNavAgent();

        // --- Target ----------------------------------------------------------

        /**
         * @brief Sets the movement target on the navmesh.
         * @param target Desired world-space goal position.
         * @return false if the target is off the navmesh or the agent is invalid.
         */
        bool setTarget(const kVec3 &target);

        /** @brief Stops the agent in place. */
        void stop();

        // --- Query -----------------------------------------------------------

        /** @brief Current world-space position of the agent. */
        kVec3 getPosition() const;

        /** @brief Current world-space velocity of the agent. */
        kVec3 getVelocity() const;

        /** @brief Returns true if the agent is actively moving toward a target. */
        bool  isMoving() const;

        /** @brief Returns true if this agent slot is active in the crowd. */
        bool  isValid() const;

        // --- Runtime parameter overrides ------------------------------------

        /**
         * @brief Overrides the agent's maximum speed at runtime.
         * @param speed New maximum speed (m/s).
         */
        void  setMaxSpeed(float speed);

        /**
         * @brief Overrides the agent's maximum acceleration at runtime.
         * @param accel New maximum acceleration (m/s²).
         */
        void  setMaxAcceleration(float accel);

        /**
         * @brief Returns the agent's current maximum speed.
         * @return Maximum speed (m/s).
         */
        float getMaxSpeed()        const;

        /**
         * @brief Returns the agent's current maximum acceleration.
         * @return Maximum acceleration (m/s²).
         */
        float getMaxAcceleration() const;

        // --- Internal (used by kNavManager) ----------------------------------

        /**
         * @brief Initialises the agent inside an existing dtCrowd.
         * @param crowd Opaque pointer to the owning dtCrowd instance.
         * @param navMeshQuery Opaque pointer to the dtNavMeshQuery used for navigation.
         * @param position Initial world-space spawn position of the agent.
         * @param config Per-agent parameters to apply on creation.
         * @return false if the agent could not be added to the crowd.
         */
        bool init(void *crowd, void *navMeshQuery,
                  const kVec3 &position, const kNavAgentConfig &config);

        /** @brief Removes the agent from the crowd. */
        void uninit();

        /**
         * @brief Returns the internal crowd agent index.
         * @return Index of this agent within the dtCrowd, or -1 if inactive.
         */
        int getAgentIndex() const;

    protected:
    private:
        struct Impl;
        Impl *m_impl;
    };

} // namespace kemena

#endif // KNAVAGENT_H
