/*
 * AUTO-GENERATED FILE. DO NOT EDIT BY HAND.
 *
 * Source: exu.json
 * Regenerate with: python tools/generate_engine_addresses.py
 */
#pragma once

#include <cstdint>

// Engine virtual addresses for BZR 2.2.301 x86, one per exu.json entry.
// Only valid once the runtime build gate (BuildValidation / RuntimeGate)
// has accepted the running executable; code that reads, calls or patches
// them must stay behind that gate. tools/validate_hardening.py rejects
// engine-range literals anywhere else in src/.
//
// The addresses are enumerators, not constexpr variables, so the generated
// code is identical to the literals they replace: MSVC folds `(T*)enumerator`
// into a constant initializer exactly like `(T*)0x...`, but for
// `(T*)constexpr_variable` in an inline variable it also emits a /include
// directive that keeps every such variable alive in the linked image.
namespace ExtraUtilities::EngineAddresses
{
	inline constexpr const char* kCatalogVersion = "2.2.301";

	namespace Lua
	{
		enum : uintptr_t
		{
			// Node (static, 32 zero bytes)
			dummynode = 0x0086EEF0u,
			// function: void __cdecl(lua_State*, Table*, int)
			setnodevector = 0x00831635u,
			// function: bool __cdecl(int pcallCode, lua_State*, const char* message)
			LuaCheckStatus = 0x004FF600u,
		};
	}

	namespace GameUI
	{
		enum : uintptr_t
		{
			// uint32_t*
			EscapeWrapperActive = 0x00918310u,
			// function: void __cdecl()
			MainShellWrapper = 0x005D42E0u,
			// function: void __cdecl(int dialog)
			PauseWrapper = 0x005D4690u,
			// uint8_t*
			PauseWrapperActive = 0x0091812Bu,
			// const char* (.rdata)
			PauseWrapperLogFormat = 0x00887A64u,
			// void**
			UiCurrentScreen = 0x00918320u,
			// uint32_t*
			MainShellWrapperActive = 0x00918324u,
			// uint32_t*
			UiCurrentScreenType = 0x00918328u,
			// void**
			SingleplayerPauseRoot = 0x009454ECu,
			// uint8_t*
			MultiplayerPauseFlag = 0x00945549u,
			// void**
			MultiplayerPauseRoot = 0x0094557Cu,
		};
	}

	namespace Camera
	{
		enum : uintptr_t
		{
			// BZR_Camera*
			View_Record_MainCam = 0x008EAAE0u,
			// float*
			zoomFactorFPP = 0x008EAD10u,
			// float*
			zoomFactorTPP = 0x008EAB10u,
			// float*
			maxZoomFactor = 0x008A2688u,
			// float*
			minZoomFactor = 0x008A25FCu,
			// void*
			viewFrustum = 0x008EABE0u,
			// int*
			currentView = 0x02CECEA0u,
			// function: void __cdecl(tagENTITY*, int)
			Set_View = 0x0061D120u,
		};
	}

	namespace Math
	{
		enum : uintptr_t
		{
			// function: void __cdecl(MAT_3D* out, MAT_3D* in)
			Matrix_Inverse = 0x008203F0u,
			// function: void __cdecl(VECTOR_3D* out, VECTOR_3D* vec, MAT_3D* mat)
			Vector_Unrotate = 0x00440300u,
		};
	}

	namespace Cheats
	{
		enum : uintptr_t
		{
			// bool*
			editMode = 0x009454B8u,
			// hook site (8 bytes)
			InfiniteAmmoHook = 0x004A7709u,
			// hook site (8 bytes)
			InfiniteScrapHook = 0x005E10D7u,
			// hook site (9 bytes)
			WeaponMaskCaptureHook = 0x0060A8C6u,
		};
	}

	namespace ControlPanel
	{
		enum : uintptr_t
		{
			// ControlPanel*
			p_controlPanel = 0x00978E20u,
			// function: void __thiscall(ControlPanel*, GameObject*)
			SelectOne = 0x004A6CD0u,
			// function: void __thiscall(ControlPanel*)
			SelectNone = 0x004A6D50u,
			// function: void __thiscall(ControlPanel*, GameObject*)
			SelectAdd = 0x004A6C70u,
			// function: char __thiscall(void*)
			HudPaletteSelector = 0x0047C070u,
			// void* (static object)
			HudPaletteSelectorThis = 0x0094F4B0u,
			// hook site (10 bytes)
			ScrapPilotHudDrawHook = 0x005C6FF0u,
			// hook site (6 bytes)
			ScrapLabelColorHook = 0x005C712Bu,
			// hook site (7 bytes)
			ScrapValueColorHook = 0x005C719Bu,
			// hook site (7 bytes)
			PilotLabelColorHook = 0x005C72F1u,
			// hook site (7 bytes)
			PilotValueColorHook = 0x005C7361u,
			// int*
			scrapLabelX = 0x0091829Cu,
			// int*
			scrapLabelY = 0x009182A0u,
			// int*
			scrapValueX = 0x0091826Cu,
			// int*
			scrapValueY = 0x00918270u,
			// int*
			pilotLabelX = 0x00918280u,
			// int*
			pilotLabelY = 0x00918284u,
			// int*
			pilotValueX = 0x00918278u,
			// int*
			pilotValueY = 0x0091827Cu,
		};
	}

	namespace Environment
	{
		enum : uintptr_t
		{
			// VECTOR_3D*
			gravityVector = 0x00871A80u,
			// int*
			timeOfDay = 0x02CD94E4u,
			// VECTOR_3D*
			sunDirection = 0x02CEB830u,
			// function: void __cdecl(int hourOfDay)
			SetTimeOfDay = 0x0068A230u,
			// function: void __cdecl()
			RefreshTerrainMasterLight = 0x0067E0E0u,
			// function (SetFog wrapper)
			FogReset = 0x00683370u,
			// IAT slot (.rdata)
			ViewportSetMaterialSchemeIat = 0x00869810u,
			// call site (call [IAT])
			ViewportSchemeCallSite0 = 0x00681585u,
			// call site (call [IAT])
			ViewportSchemeCallSite1 = 0x00682AA0u,
			// call site (call [IAT])
			ViewportSchemeCallSite2 = 0x00682EA7u,
		};
	}

	namespace GameObject
	{
		enum : uintptr_t
		{
			// function: handle __thiscall(GameObject*)
			GetHandle = 0x00462380u,
			// uintptr_t (base of object table)
			GetObj_base = 0x0260DB20u,
			// function: GameObject* __cdecl(handle)
			GetObjByHandle = 0x004DA060u,
			// function: void __thiscall(GameObject*)
			SetAsUser = 0x004DB930u,
			// void*
			p_userObject = 0x00917AFCu,
			// tagENTITY**
			user_entity_ptr = 0x00920C78u,
		};
	}

	namespace GraphicsOptions
	{
		enum : uintptr_t
		{
			// bool*
			isFullscreen = 0x009183B8u,
			// int*
			uiScaling = 0x008E77A8u,
		};
	}

	namespace Multiplayer
	{
		enum : uintptr_t
		{
			// bool*
			isNetGame = 0x00917F7Bu,
			// int*
			lives = 0x008E8D04u,
			// uint8_t*
			myNetID = 0x009180D4u,
			// bool*
			showScoreboard = 0x02A17494u,
			// function: void()
			UpdateLives = 0x006260F0u,
			// patch site (2-byte je)
			BuildObjectSyncBranch = 0x005C833Bu,
			// patch site (11 bytes)
			BuildObjectSyncCall = 0x005C833Du,
			// patch site (5 bytes)
			SkipStartingRecycler = 0x0056F014u,
		};
	}

	namespace Ogre
	{
		enum : uintptr_t
		{
			// void** (BZR EXE absolute)
			terrain_masterlight = 0x00920CA0u,
			// void** (BZR EXE absolute)
			sceneManagerStructure = 0x00920EA0u,
			// VECTOR_3D*
			worldRenderOrigin = 0x025F8E4Cu,
		};
	}

	namespace Ordnance
	{
		enum : uintptr_t
		{
			// uintptr_t (base of OrdnanceClass list)
			OrdnanceClassList = 0x009C915Cu,
			// function: Ordnance* __thiscall(OrdnanceClass*, Mat3*, OBJ76*)
			Build = 0x00586FF0u,
			// float*
			coeffBallistic = 0x008A2858u,
			// hook site (8 bytes)
			VelocityInheritanceHook = 0x004803D4u,
			// hook site (6 bytes)
			CannonLeadPositionHook = 0x0048F658u,
			// patch site (6-byte jbe)
			CannonVelocityToleranceBranch = 0x0048F639u,
		};
	}

	namespace PlayOption
	{
		enum : uintptr_t
		{
			// void*
			userProfilePtr = 0x0094672Cu,
			// uint8_t*
			difficulty = 0x025CFA1Cu,
		};
	}

	namespace Radar
	{
		enum : uintptr_t
		{
			// uint8_t*
			state = 0x008EAAACu,
			// float*
			scale = 0x008E77B0u,
			// float*
			cockpitWireframeProjectionBase = 0x008E7754u,
			// int*
			cockpitWireframeProjectionRadius = 0x009173C0u,
			// float*
			radarLeftBase = 0x009782A0u,
			// int*
			radarLeft = 0x008E77A8u,
			// int*
			radarBottom = 0x008E77ACu,
			// float*
			commandPanelLeftBase = 0x008E7918u,
			// int*
			cockpitWireframeCenterX = 0x008E7924u,
			// int*
			cockpitWireframeCenterY = 0x008E7928u,
			// float*
			edgeMinX = 0x00917388u,
			// float*
			edgeMaxX = 0x0091738Cu,
			// float*
			edgeMinZ = 0x00917390u,
			// float*
			edgeMaxZ = 0x00917394u,
			// function: void __cdecl()
			RefreshCockpitWireframeAnchor = 0x00404CF0u,
			// function: void __cdecl(int screenHeight)
			RefreshLayout = 0x00492EC0u,
			// function: RuntimePath* __cdecl(const char* name)
			FindNamedPath = 0x00460FC0u,
			// function: void __thiscall(void* self)
			RefreshEdgePathBounds = 0x0046AF20u,
			// call site (call RefreshLayout)
			RefreshLayoutCallSite0 = 0x0049325Fu,
			// call site (call RefreshLayout)
			RefreshLayoutCallSite1 = 0x0049405Bu,
		};
	}

	namespace Reticle
	{
		enum : uintptr_t
		{
			// float*
			angle = 0x025CE714u,
			// VECTOR_3D*
			position = 0x025CE79Cu,
			// float*
			range = 0x00886B20u,
			// int*
			object = 0x00979F40u,
			// MAT_3D*
			matrix = 0x025CE6F8u,
		};
	}

	namespace Satellite
	{
		enum : uintptr_t
		{
			// bool*
			state = 0x008E8F9Cu,
			// VECTOR_3D*
			cursorPos = 0x009C9194u,
			// VECTOR_3D*
			camPos = 0x009C91B4u,
			// VECTOR_3D*
			clickPos = 0x009C9188u,
			// float*
			panSpeed = 0x009C91D0u,
			// float*
			minZoom = 0x00872400u,
			// float*
			maxZoom = 0x008723F4u,
			// float*
			zoom = 0x009C91B0u,
		};
	}

	namespace SoundOptions
	{
		enum : uintptr_t
		{
			// uint8_t*
			soundStruct1 = 0x0094672Cu,
		};
	}

	namespace Steam
	{
		enum : uintptr_t
		{
			// uint64_t*
			steam64 = 0x0260B1D0u,
		};
	}

	namespace SaveGame
	{
		enum : uintptr_t
		{
			// function (native mission save)
			SaveGame = 0x004FD190u,
			// function: slot-save wrapper around SaveGame
			SaveShellGame = 0x004FDC80u,
			// char[] (save description buffer)
			saveGameDesc = 0x008E86D8u,
			// char[0x1000] (engine save directory)
			saveDirectory = 0x02CEEFE0u,
			// uint8_t* (bool; decoded at runtime, verification entry)
			missionSave = 0x009173B7u,
			// uint8_t* (bool; decoded at runtime, verification entry)
			binarySave = 0x009173B6u,
			// int32_t* (-binarysave option; decoded at runtime, verification entry)
			binarySaveSwitch = 0x008EAAB4u,
		};
	}

	namespace AiTargetSelect
	{
		enum : uintptr_t
		{
			// function: float __cdecl(const float* vector)
			VectorMagnitude = 0x00462070u,
			// function: GameObject* __thiscall(void* process, float* rangeLimit)
			OffensiveProcess_ChooseAttackTarget = 0x00583500u,
			// function: GameObject* __thiscall(void* process, float* rangeLimit)
			ScoutProcess_ChooseAttackTarget = 0x00614020u,
			// call site (call VectorMagnitude)
			ScoreCall0 = 0x004634A5u,
			// call site (call VectorMagnitude)
			ScoreCall1 = 0x00463593u,
			// call site (call VectorMagnitude)
			ScoreCall2 = 0x00463670u,
			// call site (call VectorMagnitude)
			ScoreCall3 = 0x00463A46u,
			// call site (call VectorMagnitude)
			ScoreCall4 = 0x00463B34u,
			// call site (call VectorMagnitude)
			ScoreCall5 = 0x00463C11u,
			// vftable (.rdata)
			WingmanProcess_vftable = 0x0088A6ECu,
			// vftable (.rdata)
			RocketTankProcess_vftable = 0x0088A5C0u,
			// vftable (.rdata)
			TankProcess_vftable = 0x0088AB9Cu,
			// vftable (.rdata)
			BomberProcess_vftable = 0x0088B178u,
			// vftable (.rdata)
			ScoutProcess_vftable = 0x0088AF98u,
		};
	}

	namespace ShotConvergence
	{
		enum : uintptr_t
		{
			// vftable slot (.rdata, uintptr_t)
			Wingman_UpdateWeaponAimSlot = 0x0088A4FCu,
			// vftable slot (.rdata, uintptr_t)
			TurretCraft_UpdateWeaponAimSlot = 0x00889418u,
			// function: void __thiscall(GameObject*, float)
			TurretCraft_UpdateWeaponAim = 0x005F0930u,
			// function: void __thiscall(GameObject*, float)
			Walker_UpdateWeaponAim = 0x0060F320u,
			// function: Weapon* __thiscall(void* carrier, int slot)
			Carrier_GetWeapon = 0x00417F60u,
			// function: void __cdecl(OBJ76*, MAT_3D*)
			RefreshWeaponTransform = 0x00681A00u,
		};
	}

	namespace Callbacks
	{
		enum : uintptr_t
		{
			// hook site (6 bytes)
			BulletInitHook = 0x00480363u,
			// hook site (6 bytes)
			BulletHitHook = 0x00480771u,
			// hook site (6 bytes)
			AddScrapHook = 0x005E1016u,
			// hook site (8 bytes)
			KillMessageHook = 0x0062627Fu,
		};
	}

	namespace DeathCamera
	{
		enum : uintptr_t
		{
			// hook site (5 bytes: call rel32 to CameraPush)
			ExplodePilotPushCameraCall = 0x004AD831u,
			// hook site (5 bytes: call rel32 to CameraSetFreeEye, cdecl 1 arg, caller pops)
			ExplodePilotSetViewCall = 0x004AD843u,
			// function: int __cdecl(void)
			CameraPush = 0x0061A000u,
			// function: void __cdecl(void* renderBridge)
			CameraSetFreeEye = 0x0061CF30u,
			// int32 (.data)
			CameraMode = 0x008EAAD8u,
			// void* (.data)
			CameraAttachedBridge = 0x008EACB8u,
		};
	}

	namespace ScreenFlash
	{
		enum : uintptr_t
		{
			// hook site (10 bytes: mov ecx, colorFade; call ColorFade::SetFade)
			CraftDamageFlashSite = 0x004AA6F6u,
			// hook site (10 bytes: mov ecx, colorFade; call ColorFade::SetFade)
			CraftHeatFlashSite = 0x004AB273u,
			// hook site (10 bytes: mov ecx, colorFade; call ColorFade::SetFade)
			PersonDamageFlashSite = 0x005A0D52u,
		};
	}

	namespace StatusHud
	{
		enum : uintptr_t
		{
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			WeaponRowPlateSite = 0x005DC843u,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			WeaponIconSite = 0x005DCAB5u,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			WeaponNameSite = 0x005DCB37u,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			HullLabelSite = 0x005DCDF4u,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			HullBarSite = 0x005DCEF5u,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			AmmoLabelSite = 0x005DD0C4u,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			AmmoBarSite = 0x005DD1C5u,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			AmmoShotsTextSite = 0x005DD2F8u,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			AmmoCostMarkSite1 = 0x005DD3EEu,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			AmmoCostMarkSite2 = 0x005DD41Eu,
			// hook site (5 bytes: call rel32; cdecl, caller pops)
			AmmoCostMarkSite3 = 0x005DD44Eu,
		};
	}

	namespace Turbo
	{
		enum : uintptr_t
		{
			// hook site (6 bytes)
			TurboPatchBegin = 0x00601C92u,
			// disp32 operand (float*)
			TurboToleranceOperand = 0x00601CA3u,
			// disp32 operand (float*)
			TurboGateOperand = 0x00601CADu,
		};
	}

	namespace PersonRuntime
	{
		enum : uintptr_t
		{
			// function: void __thiscall(Person*, float)
			PersonSimulate = 0x0059D340u,
			// float[12] (.data, read/write)
			PersonAnimEndTimeTable = 0x008E8E94u,
			// float[12] (.data, read/write)
			PersonAnimFirstPersonRateTable = 0x008E8EC4u,
			// const char*[12] (.data, read/write)
			PersonAnimNameTable = 0x008E8F24u,
			// float[12] (.data, read/write)
			PersonAnimWorldRateTable = 0x008E8F60u,
		};
	}

	namespace PlayerInput
	{
		enum : uintptr_t
		{
			// int8_t* (.data, level)
			WeaponFireHeld = 0x009198C0u,
			// int8_t* (.data, level)
			WeaponFireAutoHeld = 0x009198C1u,
			// code (UserProcess::Execute fire-byte read)
			WeaponFireHeldRead = 0x0060A93Du,
			// code (UserProcess::Execute auto-fire-byte read)
			WeaponFireAutoHeldRead = 0x0060A984u,
		};
	}

	namespace Soundtrack
	{
		enum : uintptr_t
		{
			// void (__cdecl*)(int,int,int,int)
			SelectTrack = 0x004377C0u,
			// void (__cdecl*)()
			Start = 0x004378F0u,
			// void (__cdecl*)()
			Stop = 0x00437A70u,
			// void (__cdecl*)()
			Pause = 0x004379D0u,
			// void (__cdecl*)()
			Resume = 0x00437A20u,
			// int (__cdecl*)(const char*)
			ResourceSize = 0x00481A60u,
			// int*
			SelectedTrack = 0x008E75F4u,
			// int*
			OggSlot = 0x008E75F8u,
			// int*
			Started = 0x00915580u,
			// int*
			Paused = 0x00915588u,
			// IDirectSoundBuffer* (__cdecl*)(int)
			GetDSBuffer = 0x0043F2A0u,
		};
	}

	namespace Terrain
	{
		enum : uintptr_t
		{
			// function: double __cdecl(double x, double z)
			HeightAt = 0x007855E0u,
		};
	}

	namespace Time
	{
		enum : uintptr_t
		{
			// float*
			SimTimeSeconds = 0x02CC1B2Cu,
			// int32_t*
			Paused = 0x02CC1B1Cu,
		};
	}

	namespace PathBlock
	{
		enum : uintptr_t
		{
			// function: void __cdecl(GameObject*, bool block)
			BlockCells = 0x00468A70u,
			// hook site (5 bytes: call rel32 to BlockCells)
			ProcessBuildingsCall = 0x00469D77u,
			// hook site (5 bytes: call rel32 to BlockCells)
			AddObjectCall = 0x0046B18Au,
			// hook site (5 bytes: call rel32 to BlockCells)
			DeleteObjectCall = 0x0046B22Au,
			// hook site (5 bytes: call rel32 to BlockCells)
			DeployUnblockCallA = 0x005AC047u,
			// hook site (5 bytes: call rel32 to BlockCells)
			DeployUnblockCallB = 0x005AC0A8u,
			// hook site (5 bytes: call rel32 to BlockCells)
			DeployBlockCallA = 0x005AC95Bu,
			// hook site (5 bytes: call rel32 to BlockCells)
			DeployBlockCallB = 0x005AD46Du,
			// hook site (5 bytes: call rel32 to BlockCells)
			DeployBlockCallC = 0x005AFF38u,
			// function: bool __cdecl(int gridX, int gridZ)
			CellIsCliff = 0x00468980u,
			// function: bool __cdecl(int gridX, int gridZ)
			CellIsSteep = 0x00468890u,
			// function: bool __cdecl(int gridX, int gridZ)
			CellIsSlope = 0x00468450u,
			// function: void __cdecl(float x0, float z0, float x1, float z1)
			InvalidateStrips = 0x0058D090u,
			// function: void __cdecl(OBJ76* root, float min[3], float max[3])
			ObjBoundingBox = 0x0062E650u,
			// function: bool(int) __cdecl(OBJ76*, int lod)
			GeoSelectLod = 0x004E3620u,
			// function: int __cdecl(const char* fileName)
			ResourceFileSize = 0x00481A60u,
			// function: ParameterDB* __thiscall(ParameterDB* self, const char* fileName)
			ParameterDbOpen = 0x00589430u,
			// function: void __thiscall(ParameterDB* self)
			ParameterDbClose = 0x00589530u,
			// function: const char* __thiscall(ParameterDB* self, uint32 sectionHash, uint32 keyHash)
			ParameterDbGetString = 0x00589620u,
			// function: uint32 __cdecl(const char* text, uint32 seed)
			ParameterHash = 0x00446460u,
			// uint8_t* (.data)
			CellTypeArray = 0x0260D178u,
			// int32
			GridMinX = 0x02CE99C0u,
			// int32
			GridMaxX = 0x02CE99A0u,
			// int32
			GridMinZ = 0x02CD9984u,
			// int32
			GridMaxZ = 0x02CE99C4u,
			// float
			GridSize = 0x02CC50E0u,
			// float
			GridScale = 0x02CC50E4u,
			// std::map<GameObject*, Area> (MSVC: head node*, size)
			BuildingAreaMap = 0x0094DE74u,
			// std::map<GameObject*, Area>
			PerimeterAreaMap = 0x0094DE44u,
		};
	}

	inline constexpr unsigned kEntryCount = 225u;
}
