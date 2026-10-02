// Copyright (C) 2026 Extra Utilities contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <cstddef>
namespace ExtraUtilities { namespace OgreScripts {
// Parses an in-memory script in the current runtime. Does not retain Lua memory
// or perform filesystem I/O. Ogre owns resources created by the script.
// True when Ogre already holds a particle_system template with this name.
// Templates survive resource-group rebuilds and new mission Lua states.
bool HasParticleTemplate(const char* name) noexcept;
bool TryParse(const char* text, std::size_t length, const char* source, const char* group) noexcept;
} }
