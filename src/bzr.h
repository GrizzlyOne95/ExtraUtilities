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

/*
* Structs and memory offsets for BZR 2.2.301
*/

#pragma once

#include <cstdint>

#include "Util/RuntimeGate.h"
#include "Util/Vec3.h"
#include "Util/EngineAddresses.generated.h"

namespace BZR
{
	// Engine addresses come from exu.json through the generated header;
	// tools/validate_hardening.py rejects raw engine literals in src/.
	namespace EngineAddresses = ::ExtraUtilities::EngineAddresses;

	// Typedefs
	using handle = unsigned int; // internally handles are unsigned ints

	// Forward declarations
	class GameObject;
	using GameObjectClass = void;
	using tagENTITY = void;
	class OBJ76;
	using AiProcess = void;
	using VECTOR_3D = ExtraUtilities::Vec3;

	using pure_virtual_t = void(__thiscall*)(void);

	struct Jammer
	{
		float maxSpeed;
		GameObject* owner;
	};

	struct Scanner
	{
		float range;
		float period;
		bool active;
		float sweep;
		GameObject* owner;
	};

	struct MAT_3D
	{
		float right_x;
		float right_y;
		float right_z;
		float up_x;
		float up_y;
		float up_z;
		float front_x;
		float front_y;
		float front_z;
		uint8_t padding[4];
		double posit_x;
		double posit_y;
		double posit_z;
	};

	using Mat3 = MAT_3D;

	//struct VECTOR_3D
	//{
	//	VECTOR_3D() : x(0), y(0), z(0) {}
	//	VECTOR_3D(float x, float y, float z) : x(x), y(y), z(z) {}
	//	VECTOR_3D(double x, double y, double z) : x(static_cast<float>(x)), y(static_cast<float>(y)), z(static_cast<float>(z)) {}
	//	float x, y, z;
	//};

	struct VECTOR_3D_LONG
	{
		double x, y, z;
		VECTOR_3D_LONG() : x(0), y(0), z(0) {}
		VECTOR_3D_LONG(double x, double y, double z) : x(x), y(y), z(z) {}
	};

	struct Frustum
	{
		VECTOR_3D near_top_left;
		VECTOR_3D near_top_right;
		VECTOR_3D near_bottom_right;
		VECTOR_3D near_bottom_left;
		VECTOR_3D far_top_left;
		VECTOR_3D far_top_right;
		VECTOR_3D far_bottom_right;
		VECTOR_3D far_bottom_left;
	};

	class BZR_Camera // todo merge camera namespaces
	{
	public:
		float Orig_x;
		float Orig_y;
		float Const_x;
		float Const_y;
		float Max_Depth;
		float Left;
		float Bottom;
		float Right;
		float Top;
		float View_Angle;
		float Tang;
		float Aspect;
		float Zoom_Factor;
		void (*Draw_Poly)(void);
		void* Buffer; // _GRAPHIC_BUFFER*
		MAT_3D Matrix;
		VECTOR_3D_LONG bSphere_Center;
		double bSphere_Radius;
		uint8_t View_Volume[0x60]; // plane class
		Frustum View_Frustum;
		VECTOR_3D_LONG View_Pyramid[5];
	};

	namespace Camera
	{
		inline auto View_Record_MainCam = (BZR_Camera*)EngineAddresses::Camera::View_Record_MainCam;

		inline auto zoomFactorFPP = (float*)EngineAddresses::Camera::zoomFactorFPP;
		inline auto zoomFactorTPP = (float*)EngineAddresses::Camera::zoomFactorTPP;
		inline auto maxZoomFactor = (float*)EngineAddresses::Camera::maxZoomFactor;
		inline auto minZoomFactor = (float*)EngineAddresses::Camera::minZoomFactor;

		enum View
		{
			// Camera types
			FIRST_PERSON = 0x100,
			THIRD_PERSON = 0x101,

			// SetView Values

			// User assignable views
			COCKPIT = 1, // F1
			NO_COCKPIT = 2, // F2
			CHASE = 4, // F3
			ORBIT = 5, // F4
			NO_HUD = 3, // F5
			EDITOR = 6, // F9
			CHEAT_SATELLITE = 7, // F10
			FREECAM = 9, // F11

			TOGGLE_SATELLITE = 8, // 9 key with satellite built and powered
			TERRAIN_EDIT = 0x2A // CTRL+E
		};

		inline auto currentView = (int*)EngineAddresses::Camera::currentView;

		using _Set_View = void (__cdecl*) (tagENTITY*, int); // 2nd param is an enum
		inline _Set_View Set_View = (_Set_View)EngineAddresses::Camera::Set_View;
	}
	using _Matrix_Inverse = void (__cdecl*) (MAT_3D* returnStoragePointer, MAT_3D* matToInverse);
	inline _Matrix_Inverse Matrix_Inverse = (_Matrix_Inverse)EngineAddresses::Math::Matrix_Inverse;

	using _Vector_Unrotate = void (__cdecl*)(VECTOR_3D* returnStoragePointer, VECTOR_3D* inputVector, MAT_3D* perspectiveMatrix);
	inline _Vector_Unrotate Vector_Unrotate = (_Vector_Unrotate)EngineAddresses::Math::Vector_Unrotate;

	namespace Cheats
	{
		constexpr uintptr_t InfiniteAmmoAddr = EngineAddresses::Cheats::InfiniteAmmoHook;
		constexpr uintptr_t InfiniteScrapAddr = EngineAddresses::Cheats::InfiniteScrapHook;
		constexpr uintptr_t WeaponMaskCaptureAddr = EngineAddresses::Cheats::WeaponMaskCaptureHook;
	}

	class ControlPanel
	{
	public:
		static inline auto p_controlPanel = (ControlPanel*)EngineAddresses::ControlPanel::p_controlPanel;

		using _SelectOne = void(__thiscall*)(ControlPanel*, GameObject*);
		static inline _SelectOne SelectOne = (_SelectOne)EngineAddresses::ControlPanel::SelectOne;

		using _SelectNone = void(__thiscall*)(ControlPanel*);
		static inline _SelectNone SelectNone = (_SelectNone)EngineAddresses::ControlPanel::SelectNone;

		using _SelectAdd = void(__thiscall*)(ControlPanel*, GameObject*);
		static inline _SelectAdd SelectAdd = (_SelectAdd)EngineAddresses::ControlPanel::SelectAdd;
	};

	namespace Environment
	{
		inline auto gravityVector = (VECTOR_3D*)EngineAddresses::Environment::gravityVector;
		inline auto timeOfDay = (int*)EngineAddresses::Environment::timeOfDay;

		using _SetTimeOfDay = void(__cdecl*)(int hourOfDay);
		inline _SetTimeOfDay SetTimeOfDay = (_SetTimeOfDay)EngineAddresses::Environment::SetTimeOfDay;

		using _RefreshTerrainMasterLight = void(__cdecl*)();
		inline _RefreshTerrainMasterLight RefreshTerrainMasterLight = (_RefreshTerrainMasterLight)EngineAddresses::Environment::RefreshTerrainMasterLight;
	}

	namespace Ogre
	{
		// Per-map Ogre render origin. Redux converts simulation/Lua positions as
		// (x - origin.x, y - origin.y, -z - origin.z) before rendering them.
		// Confirmed independently in OpenShim's chunk and multiplayer-flag render
		// paths for the supported Redux executable.
		inline constexpr uintptr_t worldRenderOriginAddress = EngineAddresses::Ogre::worldRenderOrigin;
	}

	struct EULER {
		float mass;
		float mass_inv;
		float v_mag;
		float v_mag_inv;
		float I;
		float k_i;
		VECTOR_3D v;
		VECTOR_3D omega;
		VECTOR_3D Accel;
		VECTOR_3D Alpha;
	};

	using Euler = EULER;

	class GameObject
	{
	public:
		using _GetHandle = handle(__thiscall*)(GameObject*);
		static inline _GetHandle GetHandle = (_GetHandle)(EngineAddresses::GameObject::GetHandle);

		// The object arena and handle check mirror the engine's own lookup
		// (GameObject.GetObjByHandle in exu.json): slot (h >> 20) & 0xFFF of a
		// static 0x400-byte-per-slot table, live only while the slot's serial
		// at +0x15C equals the handle's low 20 bits. A serial of 0 is a free
		// slot (GetHandle returns 0 for it).
		static constexpr uintptr_t kArenaBase = EngineAddresses::GameObject::GetObj_base;
		static constexpr uintptr_t kArenaSlotSize = 0x400;
		static constexpr uint32_t kArenaSlotCount = 0x1000;
		static constexpr uintptr_t kSerialOffset = 0x15C;

		// Credit to Janne for the arena layout -VT
		// Returns the live object for h, or nullptr for 0 and for handles whose
		// object has died or whose slot now holds a different object. Mission
		// scripts routinely keep handles of units that die, so every caller
		// must handle nullptr.
		static GameObject* GetObj(handle h) noexcept
		{
			// The arena is only at kArenaBase on the qualified executable.
			if (!ExtraUtilities::RuntimeGate::IsSupported())
			{
				return nullptr;
			}

			const uint32_t serial = h & 0xFFFFFu;
			if (serial == 0)
			{
				return nullptr;
			}

			const uintptr_t slot = kArenaBase + ((h >> 20) & 0xFFFu) * kArenaSlotSize;
			const uint32_t stored = *reinterpret_cast<const uint32_t*>(slot + kSerialOffset);
			return stored == serial ? reinterpret_cast<GameObject*>(slot) : nullptr;
		}

		// True when p is the start of an arena slot that currently holds a live
		// object, i.e. something GetObj returned. Guards APIs that accept a raw
		// GameObject* from Lua before it reaches engine code.
		static bool IsLiveArenaObject(const void* p) noexcept
		{
			if (!ExtraUtilities::RuntimeGate::IsSupported())
			{
				return false;
			}

			const uintptr_t address = reinterpret_cast<uintptr_t>(p);
			if (address < kArenaBase || address >= kArenaBase + kArenaSlotCount * kArenaSlotSize ||
				(address - kArenaBase) % kArenaSlotSize != 0)
			{
				return false;
			}
			return *reinterpret_cast<const uint32_t*>(address + kSerialOffset) != 0;
		}

		using _SetAsUser = void(__thiscall*)(GameObject*);
		static inline _SetAsUser SetAsUser = (_SetAsUser)EngineAddresses::GameObject::SetAsUser;

		// Use this to determine if you are in game since player will become null
		// after exiting a map
		static inline auto p_userObject = (void*)EngineAddresses::GameObject::p_userObject;

		// the naming is based off the 1.5 pdb, the inconsistency is intentional
		static inline auto user_entity_ptr = (tagENTITY**)EngineAddresses::GameObject::user_entity_ptr;

		uintptr_t vftableAttachable;
		uint8_t padding_1[0x14];
		uintptr_t vftableDistributedObject;
		uint8_t padding_2[0xD0];
		GameObjectClass* curPilot;
		tagENTITY* ent;
		OBJ76* obj;
		GameObjectClass* objClass;
		AiProcess* aiProcess;
		const char* label;
		int independence;
		VECTOR_3D pos;
		EULER euler;

		// offset to scanner: 0x198
		// offset to jammer: 0x19C
		// offset to carrier: 0x1A0

		// Gets the scanner object for a GameObject (the radar controller)
		BZR::Scanner* GetScanner()
		{
			BZR::Scanner* scanner;

			// didn't want to deal with pointer casting on the
			// unaligned gameobject class so here's some asm
			__asm
			{
				mov ecx, [ecx + 0x198] // ecx is always the this pointer
				mov [scanner], ecx
			}

			return scanner;
		}

		// Gets the jammer object for a GameObject (velocjam controller)
		BZR::Jammer* GetJammer()
		{
			BZR::Jammer* jammer;

			__asm
			{
				mov ecx, [ecx + 0x19C]
				mov [jammer], ecx
			}

			return jammer;
		}

		// Offsets to get to Ogre-side objects from GameObject* -> f0 -> ...
		void* GetOgreEntity()
		{
			void* entity;
			__asm
			{
				mov ecx, [ecx+0xf0]
				mov ecx, [ecx+0x94]
				mov [entity], ecx
			}
			return entity;
		}

		void* GetLight()
		{
			void* light;
			__asm
			{
				mov ecx, [ecx+0xf0]
				mov ecx, [ecx+0xa8]
				mov [light], ecx
			}
			return light;
		}
	};

	namespace PersonRuntime
	{
		// GOG/qualified Redux 2.2.301 Person::Simulate. The entry identity and
		// prologue are catalogued in exu.json; use only behind RuntimeGate.
		inline constexpr uintptr_t PersonSimulate = 0x0059D340u;
	}

	namespace GraphicsOptions
	{
		inline auto isFullscreen = (bool*)EngineAddresses::GraphicsOptions::isFullscreen;
		inline auto uiScaling = (int*)EngineAddresses::GraphicsOptions::uiScaling;
	}

	namespace Multiplayer
	{
		inline auto isNetGame = (bool*)EngineAddresses::Multiplayer::isNetGame;

		// Real life counter, does not update the scoreboard in real time however
		inline auto lives = (int*)EngineAddresses::Multiplayer::lives;

		inline auto myNetID = (uint8_t*)EngineAddresses::Multiplayer::myNetID; // ID that's used with Send() and Receive()

		inline auto showScoreboard = (bool*)EngineAddresses::Multiplayer::showScoreboard;

		// Call this function to update the scoreboard with the current life count
		using _UpdateLives = void(*)(void);
		inline _UpdateLives UpdateLives = (_UpdateLives)EngineAddresses::Multiplayer::UpdateLives;
	}

	class OBJ76
	{
	public:
		uint8_t pad_1[0x20];
		Mat3 transform;
		uint8_t pad_2[44];
		GameObject* owner;
	};

	namespace Ogre
	{
		// Absolute address
		inline auto terrain_masterlight = (void**)EngineAddresses::Ogre::terrain_masterlight; // pointer to the sun light object

		inline auto sceneManagerStructure = (void**)EngineAddresses::Ogre::sceneManagerStructure; // base of some ogre structure that leads to scene manager
		constexpr uintptr_t sceneManagerOffset = 0x08;
	}

	using DAMAGE = uint8_t[0x10];
	using gas_object = void;
	using ParticleRenderPointer = uint8_t[0x04];
	using CSteamID = uint64_t;
	using ExplosionClass = void;
	using ParticleRenderClass = void;

	class Ordnance; // Forward declaration

	class OrdnanceClass
	{
	private:
		using _Build = Ordnance*(__thiscall*)(OrdnanceClass* c, Mat3* mat, OBJ76* owner);

	public:
		uint8_t padding_1[0x08];
		OrdnanceClass* proto;
		uint32_t sig;
		char* label;
		uint8_t padding_2[0x04];
		uint64_t cfg;
		char odf[0x10];
		OBJ76* ord;
		tagENTITY* ent;
		OBJ76* freeOrd;
		ExplosionClass* xplGround;
		ExplosionClass* xplVehicle;
		ExplosionClass* xplBuilding;
		int32_t ammoCost;
		float lifeSpan;
		float shotSpeed;
		float damageValue;
		uint16_t damageTypes;
		bool notifyRemote;
		char shotSound[0x10];
		uint8_t padding_3;
		ParticleRenderClass* renderClass;
	
		static constexpr uintptr_t OrdnanceClassList = EngineAddresses::Ordnance::OrdnanceClassList;
		static inline _Build Build = (_Build)EngineAddresses::Ordnance::Build;
	};

#pragma pack(push, 1)
	class Ordnance
	{
	private:
		struct vftable_t
		{
			void(__thiscall* scalar_deleting_destructor)(Ordnance* self, uint32_t param_1);
			void(__thiscall* Init)(Ordnance* self, Mat3* mat, OBJ76* obj);
			pure_virtual_t Cleanup;
			pure_virtual_t Control;
			pure_virtual_t Simulate;
			pure_virtual_t Hit;
			pure_virtual_t Submit;
			pure_virtual_t Pack;
			pure_virtual_t Unpack;
		};

	public:
		vftable_t* vftable;
		uint8_t padding_1[0x08];
		OrdnanceClass* ordnanceClass;
		float dt;
		OBJ76* obj;
		Euler euler;
		DAMAGE damage;
		gas_object* go;
		ParticleRenderPointer renderObj;
		float lifeTime;
		CSteamID source;
		int32_t bSend;
		Mat3 initMat;
		float initTime;
		void* OgreEntity;
		void* OgreNode;
		void* OgreSkeleton;
		OBJ76* owner;
		int32_t ownerHandle;

		static inline auto coeffBallistic = (float*)EngineAddresses::Ordnance::coeffBallistic;
	};
#pragma pack(pop)

	namespace PlayOption
	{
		inline auto userProfilePtr = (void*)EngineAddresses::PlayOption::userProfilePtr;
		inline uint8_t playOptionOffset = 0x30;

		inline auto difficulty = (uint8_t*)EngineAddresses::PlayOption::difficulty;
	}

	namespace Satellite
	{
		inline auto state = (bool*)EngineAddresses::Satellite::state; // old value bugged in MP 0x00917AF8
		inline auto cursorPos = (VECTOR_3D*)EngineAddresses::Satellite::cursorPos;
		inline auto camPos = (VECTOR_3D*)EngineAddresses::Satellite::camPos;
		inline auto clickPos = (VECTOR_3D*)EngineAddresses::Satellite::clickPos;
		inline auto panSpeed = (float*)EngineAddresses::Satellite::panSpeed;
		inline auto minZoom = (float*)EngineAddresses::Satellite::minZoom;
		inline auto maxZoom = (float*)EngineAddresses::Satellite::maxZoom;
		inline auto zoom = (float*)EngineAddresses::Satellite::zoom;
	}

	namespace SoundOptions
	{
		inline auto soundStruct1 = (uint8_t*)EngineAddresses::SoundOptions::soundStruct1; // this points to the music *display* value
		inline uint8_t musicOffset = 0x2A;
	}

	namespace Steam
	{
		inline auto steam64 = (uint64_t*)EngineAddresses::Steam::steam64;
	}

	namespace Radar
	{
		inline auto state = (uint8_t*)EngineAddresses::Radar::state;
		inline auto scale = (float*)EngineAddresses::Radar::scale;
		inline auto cockpitWireframeProjectionBase = (float*)EngineAddresses::Radar::cockpitWireframeProjectionBase;
		inline auto radarLeft = (int*)EngineAddresses::Radar::radarLeft;
		inline auto radarBottom = (int*)EngineAddresses::Radar::radarBottom;
		// Screen-space centre of the cockpit radar wireframe: the world origin
		// (player) projects exactly onto this point (see 0x00493330, which adds
		// centerX and subtracts from centerY).
		inline auto cockpitWireframeCenterX = (int*)EngineAddresses::Radar::cockpitWireframeCenterX;
		inline auto cockpitWireframeCenterY = (int*)EngineAddresses::Radar::cockpitWireframeCenterY;

		struct EdgePathPoint
		{
			float x;
			float z;
		};

		struct RuntimePath
		{
			uintptr_t label;
			int32_t pointCount;
			EdgePathPoint* points;
		};

		// Recomputes the radar left anchor from the current cockpit projection base.
		using _RefreshCockpitWireframeAnchor = void(__cdecl*)();
		inline _RefreshCockpitWireframeAnchor RefreshCockpitWireframeAnchor = (_RefreshCockpitWireframeAnchor)EngineAddresses::Radar::RefreshCockpitWireframeAnchor;

		// Recalculates radar and command panel placement after HUD sizing changes.
		using _RefreshLayout = void(__cdecl*)(int screenHeight);
		inline _RefreshLayout RefreshLayout = (_RefreshLayout)EngineAddresses::Radar::RefreshLayout;

		using _FindNamedPath = RuntimePath* (__cdecl*)(const char* name);
		inline _FindNamedPath FindNamedPath = (_FindNamedPath)EngineAddresses::Radar::FindNamedPath;

		using _RefreshEdgePathBounds = void(__thiscall*)(void* self);
		inline _RefreshEdgePathBounds RefreshEdgePathBounds = (_RefreshEdgePathBounds)EngineAddresses::Radar::RefreshEdgePathBounds;
	}

	namespace Reticle
	{
		inline auto angle = (float*)EngineAddresses::Reticle::angle;
		inline auto position = (VECTOR_3D*)EngineAddresses::Reticle::position;
		inline auto range = (float*)EngineAddresses::Reticle::range;
		inline auto object = (int*)EngineAddresses::Reticle::object;
		inline auto matrix = (MAT_3D*)EngineAddresses::Reticle::matrix;
	}
}
