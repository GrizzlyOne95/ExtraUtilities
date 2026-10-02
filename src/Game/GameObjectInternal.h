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

#include "Ogre/OgreMaterialRuntime.h"
#include "Ogre/OgreEntityRuntime.h"

#include "GameObject.h"
#include "GameObjectHandle.h"

#include "Patches/GlobalTurbo.h"
#include "Util/Logging.h"
#include "LuaHelpers.h"
#include "Ogre/Ogre.h"
#include "Ogre/OgreMaterialShim.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdarg>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <lua.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Internal to the GameObject feature files (GameObject.cpp, GameObjectHandle.cpp,
// AiInspection.cpp, EntityRenderApi.cpp, MaterialApi.cpp, TerrainTrn.cpp).

namespace ExtraUtilities::Lua::GameObject
{
	namespace Detail
	{
#pragma pack(push, 1)
		struct CarrierWeaponSelectionLayout
		{
			void* owner;
			void* hardpoint[5];
			void* weapon[5];
			uint32_t existingMask;
			uint32_t selectedMask;
			uint32_t enabledMask;
			int32_t special;
			float weaponTriggerTillTime;
		};

		// UnitTask and RecycleTask layouts for 2.2.301, derived from the stores in
		// their constructors (UnitTask(me, him) 0x005FE690, RecycleTask
		// 0x005B53E0 on GOG). Both derive from AiTask, whose constructor
		// (0x00461BA0) owns +0x04. Offsets marked "ctor" are written there with
		// an identifying value; "order" means the field is not initialised by
		// the constructor and is placed by field order between verified
		// neighbours.
		struct UnitTaskLayout
		{
			void* vftable;                 // 0x00
			uint32_t aiTaskBase04;         // 0x04 AiTask base
			uint32_t curState;             // 0x08 ctor: 0xD
			uint32_t nextState;            // 0x0C ctor: -1
			BZR::GameObject* me;           // 0x10 ctor: first argument
			BZR::handle himHandle;         // 0x14 ctor: GetHandle(second argument)
			void* him;                     // 0x18 order
			uint8_t wasInTransition;       // 0x1C ctor: byte 0
			uint8_t pad_1D[0x03];
			uint32_t unknown20;            // 0x20 ctor: 0xD
			uint32_t unknown24;            // 0x24 ctor: 0
			BZR::VECTOR_3D gotoPoint;      // 0x28 ctor: me position
			BZR::VECTOR_3D goalPoint;      // 0x34 ctor: me position
			void* plan;                    // 0x40 ctor: 0
			uint32_t planPoint;            // 0x44 ctor: 0
			uint32_t fixPoint;             // 0x48 ctor: 0
			BZR::VECTOR_3D gotoForce;      // 0x4C ctor: zero vector
			BZR::VECTOR_3D gotoDir;        // 0x58 ctor: zero vector
			float braccelFactor;           // 0x64 ctor: 0.05
			float strafeFactor;            // 0x68 ctor: 0.05
			float steerFactor;             // 0x6C ctor: 3.0
			float omegaFactor;             // 0x70 ctor: 0.2
			float omegaScale;              // 0x74 ctor: 0.3
			uint32_t avoidSkip;            // 0x78 ctor: 0
			void* avoidObj;                // 0x7C ctor: 0
			void* skipObj;                 // 0x80 ctor: 0
			float nextStuck;               // 0x84 ctor: 0.0
			BZR::VECTOR_3D lastStuck;      // 0x88 ctor: zero vector
			uint32_t stuckState;           // 0x94 ctor: 0
			float skill;                   // 0x98 ctor: 0.0
			float closeSq;                 // 0x9C order
			float rangeSq;                 // 0xA0 ctor: 0.0
			float time;                    // 0xA4 ctor: 0.0
			float shotSpeed;               // 0xA8 ctor: 0.0
			float shotSpeedInv;            // 0xAC ctor: 0.0
			float pitch;                   // 0xB0 ctor: 0.0
		};

		struct RecycleTaskLayout
		{
			void* vftable;                 // 0x00
			uint32_t aiTaskBase04;         // 0x04 AiTask base
			const char* deployMsg;         // 0x08 ctor: 0
			const char* foundMsg;          // 0x0C ctor: 0
			const char* notFoundMsg;       // 0x10 ctor: 0
			const char* noDropMsg;         // 0x14 ctor: 0
			float nextStuck;               // 0x18 ctor: float
			BZR::VECTOR_3D lastStuck;      // 0x1C ctor: vector
			uint32_t stuckState;           // 0x28 ctor: 0
			BZR::GameObject* me;           // 0x2C ctor: 0
			void* subtask;                 // 0x30 ctor: 0
			BZR::VECTOR_3D lastScrap;      // 0x34 ctor: vector
			BZR::handle scrapHandle;       // 0x40 ctor: 0 (order)
			BZR::handle dropHandle;        // 0x44 ctor: 0 (order)
			uint32_t curState;             // 0x48 ctor: 0 (order)
			uint32_t nextState;            // 0x4C ctor: 1 (order)
			BZR::VECTOR_3D where;          // 0x50 ctor: vector
			float nextCheck;               // 0x5C ctor: float
			BZR::VECTOR_3D lastRecyclerPos; // 0x60 ctor: vector
		};

		// Not verified against 2.2.301: its constructor initialises too few
		// fields to confirm the layout. Read-only diagnostics use it.
		struct ScavengerProcessLayout
		{
			void* vftable;
			uint8_t pad_04[0x10];
			float oldHealth;
			uint32_t curState;
			uint32_t nextState;
			BZR::handle whoHandle;
			BZR::GameObject* craft;
			BZR::VECTOR_3D where;
			uint8_t pad_34[0x0C];
			BZR::VECTOR_3D lastScrap;
			float waitTime;
			uint8_t recycle;
			uint8_t pad_45[0x03];
			uint32_t team;
			void* escortGoal;
			void* myEscorts;
			void* task;
		};
#pragma pack(pop)

		// Pin the constructor-verified offsets so an edit cannot shift a field.
		static_assert(offsetof(UnitTaskLayout, curState) == 0x08);
		static_assert(offsetof(UnitTaskLayout, me) == 0x10);
		static_assert(offsetof(UnitTaskLayout, himHandle) == 0x14);
		static_assert(offsetof(UnitTaskLayout, wasInTransition) == 0x1C);
		static_assert(offsetof(UnitTaskLayout, gotoPoint) == 0x28);
		static_assert(offsetof(UnitTaskLayout, gotoForce) == 0x4C);
		static_assert(offsetof(UnitTaskLayout, gotoDir) == 0x58);
		static_assert(offsetof(UnitTaskLayout, braccelFactor) == 0x64);
		static_assert(offsetof(UnitTaskLayout, omegaScale) == 0x74);
		static_assert(offsetof(UnitTaskLayout, lastStuck) == 0x88);
		static_assert(offsetof(UnitTaskLayout, stuckState) == 0x94);
		static_assert(offsetof(UnitTaskLayout, pitch) == 0xB0);
		static_assert(offsetof(RecycleTaskLayout, deployMsg) == 0x08);
		static_assert(offsetof(RecycleTaskLayout, me) == 0x2C);
		static_assert(offsetof(RecycleTaskLayout, lastScrap) == 0x34);
		static_assert(offsetof(RecycleTaskLayout, where) == 0x50);
		static_assert(offsetof(RecycleTaskLayout, lastRecyclerPos) == 0x60);
		bool TryReadPointerField(void* base, uint32_t offset, void*& outPointer);
		bool TryReadUInt32Field(void* base, uint32_t offset, uint32_t& outValue);
		bool TryInterpretFloat(uint32_t rawValue, float& outValue);
		bool IsReadablePointer(const void* pointer);
		bool TryGetClassLabelFromLua(lua_State* L, BZR::handle h, std::string& outClassLabel);
		void* GetRenderableEntity(BZR::GameObject* obj);
		void* GetRenderableEntity(BZR::handle h);
		void* GetFirstSubEntity(void* entity);
		void* GetSubEntity(lua_State* L, void* entity, int idx);
	}

	using namespace Detail;
}
