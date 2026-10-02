// Exercise the instrument's scope and failure boundaries without a native process.
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(__dirname + '/capture.js', 'utf8');
function setup(options = {}) {
    const memory = new Map(), hooks = new Map(), events = [];
    let attached = 0, detached = 0;
    class Pointer {
        constructor(value) { this.value = value; }
        add(value) { return new Pointer(this.value + value); }
        sub(other) { return new Pointer(this.value - (typeof other === 'number' ? other : other.value)); }
        toString() { return '0x' + this.value.toString(16); }
        toInt32() { return this.value | 0; }
        toUInt32() { return this.value >>> 0; }
        isNull() { return this.value === 0; }
        readS32() { assert(memory.has(this.value), 'Unmapped read'); return memory.get(this.value); }
        readFloat() { return this.readS32(); }
        readU8() { return this.readS32(); }
        readUtf8String(length) { return String.fromCharCode(...Array.from({length}, (_, i) => this.add(i).readU8())); }
        readByteArray(length) {
            if (this.value >= 0x17fb && this.value < 0x1b00) {
                return Uint8Array.from(Array.from({length}, (_, i) => this.add(i).readU8())).buffer;
            }
            return new Uint8Array(length).fill(options.badBytes ? 2 : 1).buffer;
        }
    }
    const pointer = value => new Pointer(value);
    const config = {targets: {}, object_offsets: {hull_label: 16, hull_bar: 20, hull_ratio: 24,
        ammo_label: 28, ammo_bar: 32, ammo_ratio: 36}, viewport_rva: 0x100,
        max_records: options.maxRecords || 100, every: 1, sha256: 'test'};
    ['render', 'sprite', 'fill', 'text'].forEach((name, index) => {
        config.targets[name] = {rva: 0x200 + index * 0x100, bytes: [1, 1, 1]};
        const site = 0x17fb + index * 0x100;
        const displacement = (0x1000 + config.targets[name].rva) - (site + 5);
        const bytes = [options.indirectCall ? 0xff : 0xe8,
            displacement & 255, (displacement >> 8) & 255,
            (displacement >> 16) & 255, (displacement >> 24) & 255];
        bytes.forEach((value, i) => memory.set(site + i, value));
    });
    memory.set(0x1100, 1920); memory.set(0x1104, 1080);
    for (const [offset, value] of [[16, 10], [20, 11], [24, 0.5], [28, 12], [32, 13], [36, 0]]) memory.set(0x2000 + offset, value);
    for (const [offset, value] of [[8, 100], [12, 250], [16, 119], [20, 299]]) memory.set(0x3000 + offset, value);
    [52, 50, 0].forEach((byte, index) => memory.set(0x4000 + index, byte));
    const context = {Process: {arch: 'ia32', id: 123, mainModule: {name: 'battlezone98redux.exe', path: 'fixture', base: pointer(0x1000), size: 0x10000}},
        Interceptor: {attach(address, callbacks) {
            if (++attached === options.failAttach) throw new Error('attach failed');
            hooks.set(address.value, callbacks);
            return {detach() { detached++; }};
        }, flush() {}}, send(value) { events.push(JSON.parse(JSON.stringify(value))); }};
    const run = () => vm.runInNewContext(source.replace('__CONFIG__', JSON.stringify(config)), context);
    const call = (name, args, thread = 7) => {
        const index = ['render', 'sprite', 'fill', 'text'].indexOf(name);
        const invocation = {threadId: thread, context: {ecx: pointer(0x2000)}, returnAddress: pointer(0x1800 + index * 0x100)};
        const hook = hooks.get(0x1000 + config.targets[name].rva);
        hook.onEnter.call(invocation, args.map(pointer));
        return () => hook.onLeave && hook.onLeave.call(invocation);
    };
    return {run, call, events, memory, get detached() { return detached; }, get attached() { return attached; }};
}
{
    const s = setup(); s.run();
    s.call('sprite', [0x3000, 0, 11, 0, -50, 20, 100, 0x200005]);
    assert.equal(s.events.filter(e => e.event === 'sprite').length, 0, 'Ignore sprites outside native status render');
    const leave = s.call('render', []);
    s.call('sprite', [0x3000, 0, 11, 0, -50, 20, 100, 0x200005], 8);
    assert.equal(s.events.filter(e => e.event === 'sprite').length, 0, 'Ignore a different thread');
    s.call('sprite', [0x3000, 0, 11, 0, -50, 20, 100, 0x200005]);
    const bar = s.events.find(e => e.event === 'sprite');
    assert.deepEqual(bar.inferred_full_meter, [100, 200, 20, 100]);
    assert.equal(bar.displayed_ratio, 0.5);
    assert.equal(bar.call_kind, 'direct_rel32');
    assert.equal(bar.callsite_rva, 0x8fb);
    assert.equal(bar.call_target_rva, 0x300);
    assert.equal(bar.callsite_bytes[0], 0xe8);
    // The renderer chooses the live faction label after the entry probe.
    s.memory.set(0x2010, 21);
    s.call('sprite', [0x3000, 0, 21, 10, 20, 30, 40, 1]);
    assert.equal(s.events.filter(e => e.event === 'sprite').at(-1).kind, 'hull_label');
    // Even at zero fill, recover the full meter from the advanced clip and negative offset.
    s.memory.set(0x300C, 300);
    s.call('sprite', [0x3000, 0, 13, 0, -100, 20, 100, 0x200001]);
    const empty = s.events.filter(e => e.event === 'sprite').at(-1);
    assert.equal(empty.displayed_ratio, 0);
    assert.deepEqual(empty.inferred_full_meter, [100, 200, 20, 100]);
    s.call('text', [0, 0x3000, 100, 180, 0x4000]);
    assert.equal(s.events.find(e => e.event === 'text').text, '42', 'Stop before reading after the terminator');
    assert.equal(s.events.find(e => e.event === 'text').call_target_rva, 0x500);
    s.call('fill', [0x3000, 1, 2, 3, 4, 5, 0]);
    assert.equal(s.events.find(e => e.event === 'fill').call_target_rva, 0x400);
    leave();
    s.call('fill', [0x3000, 1, 2, 3, 4, 5, 0]);
    assert.equal(s.events.filter(e => e.event === 'fill').length, 1);
}
{
    const s = setup({indirectCall: true}); s.run();
    const leave = s.call('render', []);
    s.call('sprite', [0x3000, 0, 11, 0, -50, 20, 100, 0x200005]);
    assert.equal(s.events.find(e => e.event === 'sprite').call_kind, 'not_direct_rel32');
    leave();
}
{
    const s = setup(); s.run();
    const leave = s.call('render', []);
    // A rel32 CALL to a different helper must never qualify as this submit site.
    s.memory.set(0x18fc, 1);
    s.call('sprite', [0x3000, 0, 11, 0, -50, 20, 100, 0x200005]);
    assert.equal(s.events.find(e => e.event === 'sprite').call_kind, 'unexpected_target');
    leave();
}
{
    const s = setup({badBytes: true});
    assert.throws(s.run, /Live entry bytes/);
    assert.equal(s.attached, 0, 'Verify all fingerprints before attaching');
}
{
    const s = setup({failAttach: 3});
    assert.throws(s.run, /attach failed/);
    assert.equal(s.detached, 2, 'Roll back partial instrumentation');
}
{
    const s = setup({maxRecords: 1}); s.run();
    const leave = s.call('render', []);
    s.call('sprite', [0x3000, 0, 11, 0, -50, 20, 100, 0x200005]);
    s.call('sprite', [0x3000, 0, 11, 0, -50, 20, 100, 0x200005]);
    leave();
    assert.equal(s.events.filter(e => e.event === 'record_limit').length, 1);
    assert.equal(s.events.filter(e => e.event === 'sprite').length, 0);
}
console.log('Native HUD capture host checks passed');
