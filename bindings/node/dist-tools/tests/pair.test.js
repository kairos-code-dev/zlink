'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
function recvMaybe(socket) {
    const received = new zlink.Received();
    try {
        return socket.recv(received, zlink.RecvFlags.DontWait) ? received : null;
    }
    catch (error) {
        if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
            return null;
        }
        throw error;
    }
}
test('pair messaging uses Message and Received by default', () => {
    const ctx = new zlink.Context();
    const sender = new zlink.PairSocket(ctx);
    const receiver = new zlink.PairSocket(ctx);
    sender.bind('inproc://pair-contract');
    receiver.connect('inproc://pair-contract');
    sender.send().message('ping').submit();
    const received = new zlink.Received();
    receiver.recv(received);
    assert.equal(received.parts.length, 1);
    assert.ok(Object.isFrozen(received.parts));
    assert.ok(received.parts[0] instanceof zlink.Message);
    assert.equal(received.parts[0].data().toString(), 'ping');
    assert.equal(received.routingId, null);
    receiver.close();
    sender.close();
    ctx.close();
});
test('recv returns null when no message is available with DontWait', () => {
    const ctx = new zlink.Context();
    const pair = new zlink.PairSocket(ctx);
    assert.equal(recvMaybe(pair), null);
    pair.close();
    ctx.close();
});
test('recvHandler delivers multipart Message instances', () => {
    const ctx = new zlink.Context();
    const sender = new zlink.PairSocket(ctx);
    const receiver = new zlink.PairSocket(ctx);
    sender.bind('inproc://pair-handler-contract');
    receiver.connect('inproc://pair-handler-contract');
    sender.send().message('left').message('right').submit();
    const received = new zlink.Received();
    receiver.recv(received);
    assert.equal(received.routingId, null);
    assert.deepEqual(received.parts.map((part) => part.data().toString()), ['left', 'right']);
    receiver.close();
    sender.close();
    ctx.close();
});
test('pair surface stays recv-only on the canonical api', () => {
    const ctx = new zlink.Context();
    const receiver = new zlink.PairSocket(ctx);
    assert.equal(receiver.onReceive, undefined);
    assert.equal(typeof receiver.recv, 'function');
    receiver.close();
    ctx.close();
});
