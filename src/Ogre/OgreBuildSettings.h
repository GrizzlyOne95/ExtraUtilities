/* Copyright (C) 2026 GrizzlyOne95
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

#ifndef __Custom_Config_H_
#define __Custom_Config_H_

// Local OGRE 1.10 build settings shim used so EXU can compile against the
// public Ogre headers that match the Battlezone runtime DLLs.

#define OGRE_BUILD_RENDERSYSTEM_D3D9
#define OGRE_BUILD_COMPONENT_OVERLAY

#define OGRE_CONFIG_LITTLE_ENDIAN

#define OGRE_DOUBLE_PRECISION 0
#define OGRE_NODE_INHERIT_TRANSFORM 1

// ABI invariant: EXU compiles Ogre's inline container code with std::allocator
// (custom container allocator 0), while the shipped OgreMain/OgreOverlay were
// built with STLAllocator<..., CategorisedAllocPolicy> (see
// third_party/ogre-1.10.0-bzr/ABI_NOTES.md). A container filled by inline code
// here (for example Font::setGlyphTexCoords or addCodePointRange) is later
// freed by the DLL through its own allocator. That is safe only because the
// shipped CategorisedAllocPolicy is StdAllocPolicy (OGRE_MEMORY_ALLOCATOR 1,
// plain malloc/free) and both the game's MSVCR120 and EXU's UCRT allocate from
// the process heap. Exported functions whose mangled names include the custom
// allocator types (FontManager::create) cannot be linked from these headers
// and are resolved with GetProcAddress instead.
#define OGRE_MEMORY_ALLOCATOR 1
#define OGRE_CONTAINERS_USE_CUSTOM_MEMORY_ALLOCATOR 0
#define OGRE_STRING_USE_CUSTOM_MEMORY_ALLOCATOR 0

#define OGRE_MEMORY_TRACKER_DEBUG_MODE 0
#define OGRE_MEMORY_TRACKER_RELEASE_MODE 0
#define OGRE_ASSERT_MODE 0

#define OGRE_THREAD_SUPPORT 0
#define OGRE_THREAD_PROVIDER 0

#define OGRE_NO_MESHLOD 0
#define OGRE_NO_FREEIMAGE 0
#define OGRE_NO_DDS_CODEC 0
#define OGRE_NO_PVRTC_CODEC 1
#define OGRE_NO_ETC_CODEC 1
#define OGRE_NO_STBI_CODEC 0
#define OGRE_NO_ZIP_ARCHIVE 0
#define OGRE_NO_VIEWPORT_ORIENTATIONMODE 0
#define OGRE_NO_GLES2_CG_SUPPORT 1
#define OGRE_NO_GLES2_GLSL_OPTIMISER 1
#define OGRE_NO_GL_STATE_CACHE_SUPPORT 0
#define OGRE_NO_GLES3_SUPPORT 1
#define OGRE_NO_TBB_SCHEDULER 1
#define OGRE_USE_BOOST 0
#define OGRE_PROFILING 0
#define OGRE_NO_QUAD_BUFFER_STEREO 1
#define OGRE_BITES_HAVE_SDL 0

#endif
