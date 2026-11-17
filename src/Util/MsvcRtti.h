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
#include <cstring>

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

	// Unsafe: no validation, so call only inside an SEH-guarded POD shell on a
	// pointer to a polymorphic object. True if the hierarchy lists baseName
	// (decorated, e.g. ".?AVUnitTask@@").
	inline bool HasBaseUnsafe(const void* object, const char* baseName)
	{
		if (object == nullptr) return false;
		void** vtable = *reinterpret_cast<void***>(const_cast<void*>(object));
		const auto* col = reinterpret_cast<const CompleteObjectLocator*>(vtable[-1]);
		if (col->signature != 0 || col->pClassDescriptor == nullptr) return false;
		const auto* hierarchy = col->pClassDescriptor;
		if (hierarchy->numBaseClasses > 64) return false;
		for (uint32_t i = 0; i < hierarchy->numBaseClasses; ++i)
			if (std::strcmp(hierarchy->pBaseClassArray[i]->pTypeDescriptor->name, baseName) == 0) return true;
		return false;
	}

	// Unsafe, same rules: the undecorated class name (".?AVFoo@@" -> "Foo").
	template <std::size_t N>
	inline void ClassNameUnsafe(const void* object, char (&out)[N])
	{
		out[0] = '\0';
		if (object == nullptr) return;
		void** vtable = *reinterpret_cast<void***>(const_cast<void*>(object));
		const auto* col = reinterpret_cast<const CompleteObjectLocator*>(vtable[-1]);
		const char* raw = col->pTypeDescriptor->name;
		if (std::strncmp(raw, ".?AV", 4) == 0 || std::strncmp(raw, ".?AU", 4) == 0) raw += 4;
		std::size_t n = 0;
		while (n < N - 1 && raw[n] != '\0' && raw[n] != '@') { out[n] = raw[n]; ++n; }
		out[n] = '\0';
	}
}
