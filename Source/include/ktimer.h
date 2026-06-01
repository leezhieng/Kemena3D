/**
 * @file ktimer.h
 * @brief High-resolution per-frame delta-time timer.
 */

#ifndef KTIMER_H
#define KTIMER_H

#include "kexport.h"

#include <chrono>

namespace kemena
{
    /**
     * @brief Measures elapsed time between consecutive frames.
     *
     * Call tick() once per frame (before reading getDeltaTime()) to update the
     * stored delta-time value.
     */
    class KEMENA3D_API kTimer
    {
    public:
        /**
         * @brief Constructs the timer and initialises its internal reference timestamp.
         */
        kTimer();

        /**
         * @brief Records the current time and computes the delta since the last tick.
         *
         * Must be called once per frame before getDeltaTime().
         */
        void tick();

        /**
         * @brief Returns the elapsed time (in seconds) between the last two tick() calls.
         * @return Delta time in seconds.
         */
        float getDeltaTime();

    protected:
    private:
        float deltaTime; ///< Cached delta-time from the most recent tick().
        std::chrono::system_clock::time_point then; ///< Timestamp of the previous tick().
    };
}

#endif // KTIMER_H
