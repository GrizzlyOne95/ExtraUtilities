#pragma once

#include <map>
#include <string>
#include <utility>

namespace ExtraUtilities::Ogre
{
    // Keep the exact requested group, including Autodetect, in the key. A
    // lookup in one private group must never answer a lookup in another.
    // Handle owns its reference; replacement and Clear release it normally.
    template<class Handle>
    class MaterialCache
    {
    public:
        void Store(const std::string& name, const std::string& group, const Handle& handle)
        {
            if (!name.empty() && !handle.isNull())
                entries[{group, name}] = handle;
        }

        bool Find(const std::string& name, const std::string& group, Handle& result) const
        {
            const auto it = entries.find({group, name});
            if (it == entries.end() || it->second.isNull())
            {
                result = {};
                return false;
            }
            result = it->second;
            return true;
        }

        void Clear() noexcept { entries.clear(); }

    private:
        std::map<std::pair<std::string, std::string>, Handle> entries;
    };
}
