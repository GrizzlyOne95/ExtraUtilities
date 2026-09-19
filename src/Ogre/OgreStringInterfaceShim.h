/* Copyright (C) 2023-2026 VTrider
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

// Ogre ABI assumptions for Ogre::StringInterface, the generic text-keyed
// property interface that Ogre::ParticleEmitter and Ogre::ParticleAffector both
// derive from. Everything here is resolved by mangled name out of the shipped
// OgreMain.dll rather than linked, so a Redux build that does not export one of
// these symbols simply loses the feature instead of failing to load EXU.
//
// Verified against the shipped Battlezone 98 Redux OgreMain.dll (Ogre 1.10,
// 32-bit, MSVC12/MSVCP120). All symbols below are present in its export table.
//
// Note on the vtable: EXU never indexes an Ogre vtable. setParameter and
// getParameter are virtual, but ParticleEmitter and ParticleAffector do not
// override them - the base implementation walks the ParamDictionary Ogre built
// for the concrete type - so calling the exported StringInterface
// implementation directly is the correct dispatch, not a bypass.

#include <cstdint>
#include <string>

namespace ExtraUtilities::OgreAbi
{
	// bool Ogre::StringInterface::setParameter(const String&, const String&)
	inline constexpr const char* kStringInterfaceSetParameter =
		"?setParameter@StringInterface@Ogre@@UAE_NABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@0@Z";

	// Ogre::String Ogre::StringInterface::getParameter(const String&) const
	inline constexpr const char* kStringInterfaceGetParameter =
		"?getParameter@StringInterface@Ogre@@UBE?AV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@ABV34@@Z";

	// const ParameterList& Ogre::StringInterface::getParameters() const
	inline constexpr const char* kStringInterfaceGetParameters =
		"?getParameters@StringInterface@Ogre@@QBEABV?$vector@VParameterDef@Ogre@@V?$STLAllocator@VParameterDef@Ogre@@V?$CategorisedAllocPolicy@$0A@@2@@2@@std@@XZ";

	// const String& Ogre::ParticleEmitter::getType() const
	inline constexpr const char* kParticleEmitterGetType =
		"?getType@ParticleEmitter@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ";

	// const String& Ogre::ParticleAffector::getType() const
	inline constexpr const char* kParticleAffectorGetType =
		"?getType@ParticleAffector@Ogre@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ";

	// unsigned short Ogre::ParticleSystem::getNumAffectors() const
	inline constexpr const char* kParticleSystemGetNumAffectors =
		"?getNumAffectors@ParticleSystem@Ogre@@QBEGXZ";

	// ParticleAffector* Ogre::ParticleSystem::getAffector(unsigned short) const
	inline constexpr const char* kParticleSystemGetAffector =
		"?getAffector@ParticleSystem@Ogre@@QBEPAVParticleAffector@2@G@Z";

	// bool __thiscall(this, name, value)
	using StringInterfaceSetParameterFn = bool(__thiscall*)(void*, const std::string&, const std::string&);

	// Ogre::String is returned by value, so MSVC's __thiscall convention puts
	// `this` in ECX and pushes a hidden pointer to caller-provided storage as
	// the first stack argument. The callee constructs the result in place and
	// returns that same pointer.
	//
	// The storage we hand over must be a live, empty std::string: an empty
	// MSVC std::string owns no heap block, so letting OgreMain construct over
	// it cannot leak, and destroying the result on our side is the same
	// cross-CRT free this codebase already performs when it assigns into
	// Ogre-owned strings. MSVCR120 and the UCRT both allocate from
	// GetProcessHeap(), so the block released matches the heap it came from.
	using StringInterfaceGetParameterFn = std::string*(__thiscall*)(void*, std::string*, const std::string&);

	// const std::vector<ParameterDef>& __thiscall(this)
	using StringInterfaceGetParametersFn = const void*(__thiscall*)(void*);

	// const std::string& __thiscall(this)
	using GetTypeNameFn = const std::string*(__thiscall*)(void*);

	using ParticleSystemGetNumAffectorsFn = uint16_t(__thiscall*)(void*);
	using ParticleSystemGetAffectorFn = void*(__thiscall*)(void*, uint16_t);

	// Ogre::ParameterDef is { String name; String description; ParameterType
	// paramType; }. It is a plain aggregate with no vtable, so its layout is
	// two std::strings followed by a 4-byte enum.
	//
	// EXU is 32-bit MSVC and OgreMain is 32-bit MSVC12; std::string has been a
	// 24-byte object (16-byte SSO union, size, capacity, empty allocator) in
	// every MSVC from 2010 to 2022, which the static_assert below pins. If a
	// future toolchain changes that, the assert fires at compile time rather
	// than letting the enumerator walk a mis-strided array at runtime.
	struct ParameterDefLayout
	{
		std::string name;
		std::string description;
		int32_t paramType;
	};

	static_assert(sizeof(std::string) == 24, "MSVC 32-bit std::string is expected to be 24 bytes");
	static_assert(sizeof(ParameterDefLayout) == 52, "Ogre::ParameterDef is expected to be two strings plus an enum");

	// Ogre's ParameterList is std::vector<ParameterDef, STLAllocator<...>>.
	// STLAllocator is stateless, and Ogre release builds compile with
	// _ITERATOR_DEBUG_LEVEL=0, so the vector is exactly three pointers with no
	// debug proxy. This is read-only: the enumerator sanity-checks the three
	// pointers before dereferencing anything and gives up otherwise.
	struct ParameterListLayout
	{
		const ParameterDefLayout* first;
		const ParameterDefLayout* last;
		const ParameterDefLayout* end;
	};

	static_assert(sizeof(ParameterListLayout) == 12, "MSVC 32-bit release std::vector is expected to be three pointers");

	// A ParamDictionary never holds anything like this many entries - the
	// largest stock ParticleFX type publishes well under 32 - so a larger count
	// means the pointers are not really a ParameterList and the read is
	// abandoned.
	inline constexpr uint32_t kMaxParameterDefs = 256;
}
