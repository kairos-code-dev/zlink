// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('../dist');
const ctx = new zlink.Context();
const server = new zlink.PairSocket(ctx);
const client = new zlink.PairSocket(ctx);
try {
    server.bind('inproc://example-pair-recv');
    client.connect('inproc://example-pair-recv');
    client.send([zlink.Message.copyOf('alpha'), zlink.Message.copyOf('beta')]);
    const received = server.receive();
    assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['alpha', 'beta']);
    console.log('pair recv sample ok');
}
finally {
    client.close();
    server.close();
    ctx.close();
}
