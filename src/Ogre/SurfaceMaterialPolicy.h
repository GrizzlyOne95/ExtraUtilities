#pragma once
#include <cstddef>
#include <set>
#include <string>

namespace ExtraUtilities { namespace SurfaceMaterialPolicy
{
    // Native debris finishes live under this prefix; Lua-facing APIs never see them.
    inline bool IsNativeChunkName(const std::string& name) noexcept
    {
        static const char prefix[] = "exu_surface_chunk_";
        return name.compare(0, sizeof(prefix) - 1, prefix) == 0;
    }

    // Small capped name set (negative caches, leftover-name tracking). Add
    // returns false only when a new name did not fit.
    class BoundedNameSet
    {
    public:
        explicit BoundedNameSet(std::size_t capacity) : capacity_(capacity) {}
        bool Contains(const std::string& name) const { return names_.count(name) != 0; }
        bool Add(const std::string& name)
        {
            if (names_.count(name)) return true;
            if (names_.size() >= capacity_) return false;
            names_.insert(name);
            return true;
        }
        void Erase(const std::string& name) { names_.erase(name); }
        void Clear() { names_.clear(); }
        std::size_t Size() const { return names_.size(); }
    private:
        std::size_t capacity_;
        std::set<std::string> names_;
    };
} }
