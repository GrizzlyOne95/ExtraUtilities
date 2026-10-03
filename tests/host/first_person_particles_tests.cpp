/* Copyright (C) 2026 GrizzlyOne95
 *
 * This file is part of Extra Utilities.
 */

#include "HostTest.h"
#include "Game/FirstPersonParticles.h"

#include <string>

namespace
{
	using namespace ExtraUtilities::Lua::FirstPersonParticles;
	using HostTest::Expect;

	// Identity tokens only; the book never dereferences them.
	int g_entityA = 0;
	int g_entityB = 0;
	int g_tagA = 0;
	int g_tagA2 = 0;
	int g_tagB = 0;

	const void* const kEntityA = &g_entityA;
	const void* const kEntityB = &g_entityB;
	const void* const kTagA = &g_tagA;
	const void* const kTagA2 = &g_tagA2;
	const void* const kTagB = &g_tagB;

	const Offset kMuzzle{ 0.0f, 0.05f, 0.62f };

	ObservedParent On(const void* tagPoint, const void* entity)
	{
		ObservedParent observed;
		observed.tagPoint = tagPoint;
		observed.tagPointEntity = entity;
		return observed;
	}

	const ObservedParent kDetached{};

	void FirstAttachAndIdempotence()
	{
		BindingBook book;
		Expect(book.Decide("flash", kEntityA, "gun", kMuzzle, kDetached) == Decision::Attach,
			"unknown name attaches");
		Expect(book.Record("flash", kEntityA, "gun", kMuzzle, kTagA), "record succeeds");
		Expect(book.TargetGeneration() == 1, "first binding starts generation 1");
		Expect(book.Decide("flash", kEntityA, "gun", kMuzzle, On(kTagA, kEntityA)) == Decision::AlreadyAttached,
			"same entity/bone/offset with live parent is a no-op");
		Expect(book.IsAttached("flash", kEntityA, On(kTagA, kEntityA)), "reported attached");
		Expect(!book.IsAttached("flash", nullptr, On(kTagA, kEntityA)), "no FP entity -> not attached");
	}

	void ParameterChangesReattach()
	{
		BindingBook book;
		book.Record("flash", kEntityA, "gun", kMuzzle, kTagA);
		Expect(book.Decide("flash", kEntityA, "barrel", kMuzzle, On(kTagA, kEntityA)) == Decision::Attach,
			"different bone re-attaches");
		Offset moved = kMuzzle;
		moved.z += 0.01f;
		Expect(book.Decide("flash", kEntityA, "gun", moved, On(kTagA, kEntityA)) == Decision::Attach,
			"different offset re-attaches");
	}

	void TargetChange()
	{
		BindingBook book;
		book.Record("flash", kEntityA, "gun", kMuzzle, kTagA);
		book.Record("smoke", kEntityA, "gun", kMuzzle, kTagA2);
		Expect(book.TargetGeneration() == 1, "second binding on the same entity keeps the generation");

		// Respawn: entity A destroyed (Ogre detached the systems), entity B new.
		Expect(book.Decide("flash", kEntityB, "gun", kMuzzle, kDetached) == Decision::Attach,
			"new FP entity re-attaches");
		Expect(!book.IsAttached("flash", kEntityB, kDetached), "not attached to the new entity yet");
		book.Record("flash", kEntityB, "gun", kMuzzle, kTagB);
		Expect(book.TargetGeneration() == 2, "binding on a new entity bumps the generation");
		book.Record("smoke", kEntityB, "gun", kMuzzle, kTagA);
		Expect(book.TargetGeneration() == 2, "only the first binding per entity bumps it");
		Expect(book.Decide("flash", kEntityB, "gun", kMuzzle, On(kTagB, kEntityB)) == Decision::AlreadyAttached,
			"steady state on the new entity");
	}

	void RecycledEntityAddress()
	{
		BindingBook book;
		book.Record("flash", kEntityA, "gun", kMuzzle, kTagA);
		// Entity A destroyed and a new FP entity allocated at the same address:
		// the system is no longer parented (Entity::_deinitialise), so the
		// live parent disagrees with the record.
		Expect(book.Decide("flash", kEntityA, "gun", kMuzzle, kDetached) == Decision::Attach,
			"recycled address with a detached system re-attaches");
		Expect(!book.IsAttached("flash", kEntityA, kDetached), "recycled address is not attached");
		// Moved by another API onto another TagPoint of the same entity.
		Expect(book.Decide("flash", kEntityA, "gun", kMuzzle, On(kTagA2, kEntityA)) == Decision::Attach,
			"different live TagPoint re-attaches");
		// Still on the old TagPoint but that TagPoint now reports another entity.
		Expect(book.Decide("flash", kEntityA, "gun", kMuzzle, On(kTagA, kEntityB)) == Decision::Attach,
			"TagPoint owned by another entity re-attaches");
	}

	void ForgetAndReset()
	{
		BindingBook book;
		book.Record("flash", kEntityA, "gun", kMuzzle, kTagA);
		Expect(book.Forget("flash"), "forget reports the binding");
		Expect(!book.Forget("flash"), "second forget is a no-op");
		Expect(book.Decide("flash", kEntityA, "gun", kMuzzle, On(kTagA, kEntityA)) == Decision::Attach,
			"forgotten binding re-attaches even if Ogre still has it there");

		book.Record("flash", kEntityA, "gun", kMuzzle, kTagA);
		const auto generation = book.TargetGeneration();
		book.Clear();
		Expect(book.Count() == 0, "clear drops all bindings");
		Expect(book.TargetGeneration() == generation, "clear keeps the generation counter");
		book.Record("flash", kEntityA, "gun", kMuzzle, kTagA);
		Expect(book.TargetGeneration() == generation + 1,
			"first binding after a mission reset counts as a target change even at the same address");
	}

	void Capacity()
	{
		BindingBook book;
		for (std::size_t i = 0; i < BindingBook::kMaxBindings; ++i)
		{
			Expect(book.Record("p" + std::to_string(i), kEntityA, "gun", kMuzzle, kTagA), "fill");
		}
		Expect(!book.CanRecord("overflow"), "new name refused when full");
		Expect(!book.Record("overflow", kEntityA, "gun", kMuzzle, kTagA), "record refused when full");
		Expect(book.CanRecord("p0"), "existing name still updatable when full");
		Expect(book.Record("p0", kEntityB, "gun", kMuzzle, kTagB), "existing name updates when full");
		book.Forget("p1");
		Expect(book.CanRecord("overflow"), "room after a forget");
	}
}

int main()
{
	FirstAttachAndIdempotence();
	ParameterChangesReattach();
	TargetChange();
	RecycledEntityAddress();
	ForgetAndReset();
	Capacity();
	return HostTest::Finish("first-person particle");
}
