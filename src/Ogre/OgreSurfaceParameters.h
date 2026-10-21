#pragma once
#include <string>

namespace ExtraUtilities { namespace OgreSurfaceParameters
{
    // Regular named uniforms on one material pass; absent/wrong-type uniforms
    // are skipped. No Renderable custom-parameter carryover is possible.
    bool SetVector(void* pass, const std::string& name, const float value[4]) noexcept;
    bool Clone(const std::string& source, const std::string& name) noexcept;
    bool Assign(void* subEntity, const std::string& name) noexcept;
    void NotifyVariant(void* entity, unsigned int index, const std::string& before, const std::string& after) noexcept;
    int SetOwnedVector(const std::string& material, const std::string& name, const float value[4]) noexcept;
    int SetTexture(const std::string& material, const std::string& alias, const std::string& texture) noexcept;
    bool Release(const std::string& name) noexcept;
    void Clear() noexcept;
    void EnableChunks(bool enabled) noexcept;
    // Shared source-preserving finish: one variant per source, never per piece.
    std::string ChunkMaterial(const std::string& source) noexcept;
    void FinishChunk(void* entity) noexcept;
} }
