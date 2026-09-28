/**
 * @file blend_weights.h
 * @brief Shared blend-tree weight calculation.
 *
 * The animator-controller runtime (Manager::stepAnimators) and the Animator
 * editor's embedded blend-tree preview must agree on exactly how a 1D axis or
 * 2D plane maps to per-motion weights, otherwise the preview would not match
 * what plays in-game. Both include this header instead of each implementing
 * their own copy.
 *
 * Only the 2D (plane) weighting lives here; the 1D case is a simple two-motion
 * lerp that each caller writes inline next to the axis it is blending.
 */

#ifndef BLEND_WEIGHTS_H
#define BLEND_WEIGHTS_H

#include <algorithm>
#include <cmath>
#include <vector>

namespace kblend
{
    /** @brief A motion's position on the 2D blend plane. */
    struct Point2
    {
        float x = 0.0f;
        float y = 0.0f;

        Point2() = default;
        Point2(float px, float py) : x(px), y(py) {}
    };

    /** @brief Signed area (twice) of the triangle a→b→c; sign encodes winding. */
    inline float cross2(const Point2 &a, const Point2 &b, const Point2 &c)
    {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    }

    /** @brief Clamped (0..1) projection parameter of p onto segment a→b. */
    inline float projectT(const Point2 &a, const Point2 &b, float px, float py)
    {
        const float abx  = b.x - a.x;
        const float aby  = b.y - a.y;
        const float len2 = abx * abx + aby * aby;
        if (len2 < 1e-12f)
            return 0.0f;
        const float t = ((px - a.x) * abx + (py - a.y) * aby) / len2;
        return std::max(0.0f, std::min(1.0f, t));
    }

    /**
     * @brief Weighted mix of motions on a 2D blend plane.
     *
     * Produces a *local* blend: only the motions that actually frame the
     * parameter contribute. Weights are the barycentric coordinates inside the
     * tightest triangle of motion points enclosing (px, py); when the parameter
     * lies outside the authored hull, the two endpoints of the nearest
     * motion-pair segment are blended instead.
     *
     * A point inside a triangle always has that triangle's vertices as its
     * three nearest motions, so the enclosing triangle is the candidate triple
     * whose barycentric-weighted distance to the point is smallest.
     *
     * @param pts Motion positions (one per motion, in caller order).
     * @param px  Parameter X.
     * @param py  Parameter Y.
     * @return One weight per entry of @p pts (sums to 1).
     */
    inline std::vector<float> weights2D(const std::vector<Point2> &pts, float px, float py)
    {
        const size_t n = pts.size();
        std::vector<float> weights(n, 0.0f);
        if (n == 0)
            return weights;
        if (n == 1)
        {
            weights[0] = 1.0f;
            return weights;
        }

        const Point2 q(px, py);
        const float eps = 1e-4f;

        // 1) Barycentric weights of the tightest triangle enclosing the point.
        bool  found     = false;
        float bestScore = 1e30f;
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j)
                for (size_t k = j + 1; k < n; ++k)
                {
                    const float denom = cross2(pts[i], pts[j], pts[k]);
                    if (std::fabs(denom) < 1e-9f)
                        continue; // collinear triple — cannot frame the point

                    float wi = cross2(pts[j], pts[k], q) / denom;
                    float wj = cross2(pts[k], pts[i], q) / denom;
                    float wk = cross2(pts[i], pts[j], q) / denom;
                    if (wi < -eps || wj < -eps || wk < -eps)
                        continue; // point outside this triangle

                    wi = std::max(0.0f, wi);
                    wj = std::max(0.0f, wj);
                    wk = std::max(0.0f, wk);
                    const float sum = wi + wj + wk;
                    if (sum < 1e-9f)
                        continue;
                    wi /= sum;
                    wj /= sum;
                    wk /= sum;

                    const float di = std::hypot(pts[i].x - px, pts[i].y - py);
                    const float dj = std::hypot(pts[j].x - px, pts[j].y - py);
                    const float dk = std::hypot(pts[k].x - px, pts[k].y - py);
                    const float score = wi * di + wj * dj + wk * dk;

                    if (!found || score < bestScore)
                    {
                        found     = true;
                        bestScore = score;
                        std::fill(weights.begin(), weights.end(), 0.0f);
                        weights[i] = wi;
                        weights[j] = wj;
                        weights[k] = wk;
                    }
                }
        if (found)
            return weights;

        // 2) Outside the authored hull: blend along the nearest motion pair.
        float bestDist = 1e30f;
        for (size_t i = 0; i < n; ++i)
            for (size_t j = i + 1; j < n; ++j)
            {
                const float t  = projectT(pts[i], pts[j], px, py);
                const float cx = pts[i].x + (pts[j].x - pts[i].x) * t;
                const float cy = pts[i].y + (pts[j].y - pts[i].y) * t;
                const float d  = std::hypot(cx - px, cy - py);
                if (d < bestDist)
                {
                    bestDist = d;
                    std::fill(weights.begin(), weights.end(), 0.0f);
                    weights[i] = 1.0f - t;
                    weights[j] = t;
                }
            }
        if (bestDist >= 1e30f)
            weights[0] = 1.0f; // single unique point — degenerate guard
        return weights;
    }
}

#endif // BLEND_WEIGHTS_H
