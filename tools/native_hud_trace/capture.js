// Qualification instrument only. Observe arguments; never change HUD state.
// The host verifies the exact on-disk GOG hash before constructing CONFIG.
'use strict';
const config = __CONFIG__;
const module = Process.mainModule;
if (Process.arch !== 'ia32' || module.name.toLowerCase() !== 'battlezone98redux.exe') {
    throw new Error('Expected the 32-bit Redux main module');
}
const targets = {};
for (const [name, item] of Object.entries(config.targets)) {
    const address = module.base.add(item.rva);
    const bytes = Array.from(new Uint8Array(address.readByteArray(item.bytes.length)));
    if (bytes.some((value, index) => value !== item.bytes[index])) {
        throw new Error('Live entry bytes disagree with the hashed executable: ' + name);
    }
    targets[name] = address;
}

let sequence = 0;
let renderCount = 0;
let recordCount = 0;
let capped = false;
const frames = new Map();
const listeners = [];
function record(value) {
    if (recordCount >= config.max_records) {
        if (!capped) { capped = true; send({event: 'record_limit', limit: config.max_records}); }
        return;
    }
    recordCount++;
    send(value);
}
function active(thread) {
    const stack = frames.get(thread);
    return stack && stack.length ? stack[stack.length - 1] : null;
}
function pane(buffer) {
    return [8, 12, 16, 20].map(offset => buffer.add(offset).readS32());
}
function fault(event, error) {
    record({event: 'read_error', operation: event, message: String(error)});
}
function boundedText(address) {
    if (address.isNull()) return null;
    let length = 0;
    while (length < 128 && address.add(length).readU8() !== 0) length++;
    return length === 0 ? '' : address.readUtf8String(length);
}

try {
    listeners.push(Interceptor.attach(targets.render, {
        onEnter() {
            const frame = {number: ++sequence, selected: ++renderCount % config.every === 1 || config.every === 1};
            const stack = frames.get(this.threadId) || [];
            stack.push(frame);
            frames.set(this.threadId, stack);
            this.frame = frame;
            if (!frame.selected) return;
            try {
                const object = this.context.ecx;
                const offsets = config.object_offsets;
                frame.ids = {};
                for (const name of ['hull_label', 'hull_bar', 'ammo_label', 'ammo_bar']) {
                    frame.ids[name] = object.add(offsets[name]).readS32();
                }
                frame.hull_ratio = object.add(offsets.hull_ratio).readFloat();
                frame.ammo_ratio = object.add(offsets.ammo_ratio).readFloat();
                if (!Number.isFinite(frame.hull_ratio) || !Number.isFinite(frame.ammo_ratio)) {
                    throw new Error('Non-finite native display ratio');
                }
                const viewport = module.base.add(config.viewport_rva);
                record({event: 'status_frame', frame: frame.number, thread: this.threadId,
                    object: object.toString(), hull_ratio: frame.hull_ratio, ammo_ratio: frame.ammo_ratio,
                    ids: frame.ids, viewport: [viewport.readS32(), viewport.add(4).readS32()]});
            } catch (error) { frame.selected = false; fault('status_frame', error); }
        },
        onLeave() {
            const stack = frames.get(this.threadId);
            if (stack) { stack.pop(); if (!stack.length) frames.delete(this.threadId); }
        }
    }));
    listeners.push(Interceptor.attach(targets.sprite, {
        onEnter(args) {
            const frame = active(this.threadId);
            if (!frame || !frame.selected) return;
            try {
                const id = args[2].toInt32();
                const kind = Object.keys(frame.ids).find(name => frame.ids[name] === id) || 'status_sprite';
                const clip = pane(args[0]);
                const xywh = [3, 4, 5, 6].map(index => args[index].toInt32());
                const flags = args[7].toUInt32();
                const value = {event: 'sprite', frame: frame.number, kind, id, clip, xywh, flags,
                    return_address: this.returnAddress.toString(),
                    return_rva: this.returnAddress.sub(module.base).toString()};
                if ((kind === 'hull_bar' || kind === 'ammo_bar') && (flags & 0x200000)) {
                    // Native bars submit y=-missing against a pane whose top
                    // has already advanced by missing. Keep this distinct from
                    // the sprite's submitted quad dimensions and atlas bounds.
                    const top = clip[1] + xywh[1];
                    value.inferred_full_meter = [clip[0], top, clip[2] - clip[0] + 1, clip[3] - top + 1];
                    value.displayed_ratio = kind === 'hull_bar' ? frame.hull_ratio : frame.ammo_ratio;
                }
                record(value);
            } catch (error) { fault('sprite', error); }
        }
    }));
    listeners.push(Interceptor.attach(targets.fill, {
        onEnter(args) {
            const frame = active(this.threadId);
            if (!frame || !frame.selected) return;
            try {
                record({event: 'fill', frame: frame.number, clip: pane(args[0]),
                    edges: [1, 2, 3, 4].map(index => args[index].toInt32()),
                    color: args[5].toUInt32(), mode: args[6].toInt32(),
                    return_rva: this.returnAddress.sub(module.base).toString()});
            } catch (error) { fault('fill', error); }
        }
    }));
    listeners.push(Interceptor.attach(targets.text, {
        onEnter(args) {
            const frame = active(this.threadId);
            if (!frame || !frame.selected) return;
            try {
                record({event: 'text', frame: frame.number, clip: pane(args[1]),
                    xy: [args[2].toInt32(), args[3].toInt32()],
                    text: boundedText(args[4]),
                    return_rva: this.returnAddress.sub(module.base).toString()});
            } catch (error) { fault('text', error); }
        }
    }));
    Interceptor.flush();
    send({event: 'ready', pid: Process.id, module_path: module.path, base: module.base.toString(),
        targets: Object.fromEntries(Object.entries(targets).map(([name, address]) => [name, address.toString()])),
        fingerprints: config.targets, sha256: config.sha256});
} catch (error) {
    for (const listener of listeners) listener.detach();
    throw error;
}
