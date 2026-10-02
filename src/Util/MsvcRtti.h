/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 *
 * Extra Utilities is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 */

#pragma once

#include <cstddef>
#include <cstdint>

// The 32-bit MSVC RTTI records Redux's classes carry, as read by the AI
// inspection bindings, the Ordnance* validity check and AiTargetSelect's hook
// identity check. vftable[-1] is the class's CompleteObjectLocator.
//
// These are raw records in the executable's image. Every read of one belongs
// under SEH or behind a readable-range check.
namespace ExtraUtilities::MsvcRtti
{
	struct Pmd
	{
		int mdisp;
		int pdisp;
		int vdisp;
	};

	struct TypeDescriptor
	{
		void* pVFTable;
		void* spare;
		char name[1]; // NUL-terminated decorated name, e.g. ".?AVOrdnance@@"
	};

	struct BaseClassDescriptor
	{
		TypeDescriptor* pTypeDescriptor;
		uint32_t numContainedBases;
		Pmd where;
		uint32_t attributes;
	};

	struct ClassHierarchyDescriptor
	{
		uint32_t signature;
		uint32_t attributes;
		uint32_t numBaseClasses;
		BaseClassDescriptor** pBaseClassArray;
	};

	struct CompleteObjectLocator
	{
		uint32_t signature; // 0 for 32-bit images
		uint32_t offset;
		uint32_t cdOffset;
		TypeDescriptor* pTypeDescriptor;
		ClassHierarchyDescriptor* pClassDescriptor;
	};

#if defined(_M_IX86)
	static_assert(offsetof(TypeDescriptor, name) == 0x08);
	static_assert(offsetof(BaseClassDescriptor, attributes) == 0x14);
	static_assert(offsetof(ClassHierarchyDescriptor, pBaseClassArray) == 0x0C);
	static_assert(offsetof(CompleteObjectLocator, pTypeDescriptor) == 0x0C);
	static_assert(offsetof(CompleteObjectLocator, pClassDescriptor) == 0x10);
#endif
}
