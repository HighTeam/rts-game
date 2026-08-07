#pragma once

#include "sim/snapshot/entity_snapshot_key.hpp"

namespace aoa::sim::components {

struct EntitySnapshotIdentity {
    snapshot::EntitySnapshotKey key{};
};

} // namespace aoa::sim::components
