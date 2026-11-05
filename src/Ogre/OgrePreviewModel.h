// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "Game/ConstructionPreviewCore.h"
#include <string>

namespace ExtraUtilities::OgrePreviewModel
{
    // Renderer-only entity: no arena handle, AI, collision, shadow or picking.
    struct Model
    {
        void* owner = nullptr;
        void* parent = nullptr;
        void* node = nullptr;
        void* entity = nullptr;
        bool attached = false;
        std::string mesh, material;
    };
    bool Available() noexcept;
    bool Show(Model& model, const std::string& mesh, const std::string& material,
        const ConstructionPreviewCore::Vector& position, const ConstructionPreviewCore::Quaternion& rotation);
    void Hide(Model& model) noexcept;
    void Destroy(Model& model) noexcept;
}
