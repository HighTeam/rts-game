#pragma once

#include "render/sim_render_snapshot.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace aoa::render {

// Local-only "last what I've seen" poses for enemy buildings.
class BuildingSightMemory {
public:
    void observe_visible(const RenderEntityPose& pose)
    {
        if (pose.entity == entt::null || pose.health_current <= 0 || !pose.is_enemy) {
            return;
        }

        RenderEntityPose remembered = pose;
        remembered.shrouded = true;
        entries_[static_cast<std::uint32_t>(pose.entity)] = remembered;
    }

    void forget(const entt::entity entity)
    {
        entries_.erase(static_cast<std::uint32_t>(entity));
    }

    [[nodiscard]] const RenderEntityPose* find(const entt::entity entity) const
    {
        const auto found = entries_.find(static_cast<std::uint32_t>(entity));
        if (found == entries_.end()) {
            return nullptr;
        }

        return &found->second;
    }

    [[nodiscard]] std::vector<RenderEntityPose> remembered_poses() const
    {
        std::vector<RenderEntityPose> poses{};
        poses.reserve(entries_.size());
        for (const auto& [entity_id, pose] : entries_) {
            (void)entity_id;
            poses.push_back(pose);
        }
        return poses;
    }

    void clear() { entries_.clear(); }

private:
    std::unordered_map<std::uint32_t, RenderEntityPose> entries_{};
};

} // namespace aoa::render
