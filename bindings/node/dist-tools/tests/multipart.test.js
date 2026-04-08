'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');
test('pair sockets send and receive multipart through canonical api', () => {
    const ctx = new zlink.Context();
    const left = new zlink.PairSocket(ctx);
    const right = new zlink.PairSocket(ctx);
    left.bind('inproc://multipart-contract');
    right.connect('inproc://multipart-contract');
    right.send([
        'a',
        Buffer.from('b')
    ]);
    const received = left.recv();
    assert.deepEqual(received.parts.map((part) => part.data.toString()), ['a', 'b']);
    right.close();
    left.close();
    ctx.close();
});
