// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Abraham Joseph Okayli Masaryk
// SoundNuts — registry.hpp
// Owns the curated attractor systems and wires each one's RegimeMap from the
// baked offline sweep (registry_data.hpp). No runtime file I/O or JSON parser.
// JUCE-free, header-only.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "attractor_system.hpp"
#include "registry_data.hpp"
#include "generated_systems.hpp"      // ~90 dysts flows, codegen'd
#include "systems/lorenz.hpp"
#include "systems/rossler.hpp"
#include "systems/pendulum.hpp"

namespace soundnuts {

class Registry {
public:
    Registry() {
        // Construct each concrete system and populate its normalization/regime map
        // from the baked pipeline data while we still hold the concrete type.
        auto lorenz = std::make_unique<systems::Lorenz>();
        fillRegimeMap("lorenz", lorenz->mutableRegimeMap());
        systems_.push_back(std::move(lorenz));

        auto rossler = std::make_unique<systems::Rossler>();
        fillRegimeMap("rossler", rossler->mutableRegimeMap());
        systems_.push_back(std::move(rossler));

        auto pendulum = std::make_unique<systems::Pendulum>();
        fillRegimeMap("pendulum", pendulum->mutableRegimeMap());
        systems_.push_back(std::move(pendulum));

        // Codegen'd dysts flows (each self-contains its baked phase-diagram map).
        gen::forEachGenerated([this](auto s) { systems_.push_back(std::move(s)); });
    }

    [[nodiscard]] int size() const { return static_cast<int>(systems_.size()); }

    [[nodiscard]] const AttractorSystem& system(int i) const { return *systems_[clampIndex(i)]; }
    [[nodiscard]] AttractorSystem&       system(int i)       { return *systems_[clampIndex(i)]; }

    [[nodiscard]] std::string name(int i) const { return systems_[clampIndex(i)]->name(); }
    [[nodiscard]] std::string id(int i)   const { return systems_[clampIndex(i)]->id(); }

private:
    [[nodiscard]] std::size_t clampIndex(int i) const {
        if (systems_.empty()) return 0;
        if (i < 0) return 0;
        if (i >= static_cast<int>(systems_.size())) return systems_.size() - 1;
        return static_cast<std::size_t>(i);
    }

    std::vector<std::unique_ptr<AttractorSystem>> systems_;
};

}  // namespace soundnuts
