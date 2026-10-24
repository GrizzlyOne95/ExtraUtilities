// Copyright (C) 2026 GrizzlyOne95
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "Game/BuildEventsCore.h"
#include <cassert>
using namespace ExtraUtilities::BuildEventsCore;
int main()
{
    Queue<2> queue;
    Event out, event;
    event.producer = 123; event.odf[0] = 'a';
    queue.Push(event); event.type = Type::Completed; event.built = 456; queue.Push(event);
    assert(queue.Pop(out) && out.sequence == 1 && out.type == Type::Queued);
    assert(queue.Pop(out) && out.sequence == 2 && out.built == 456);
    assert(!queue.Pop(out));
    queue.Push(event); event.type = Type::Cancelled; queue.Push(event); queue.Push(event);
    assert(queue.dropped == 1 && queue.size == 2);
    assert(queue.Pop(out) && out.sequence == 4 && out.type == Type::Cancelled);
    queue.Clear();
    assert(queue.sequence == 0 && queue.dropped == 0 && !queue.Pop(out));
}
