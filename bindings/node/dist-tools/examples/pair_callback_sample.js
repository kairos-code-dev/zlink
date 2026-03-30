// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('../dist');
async function main() {
    const ctx = new zlink.Context();
    const server = new zlink.PairSocket(ctx);
    const client = new zlink.PairSocket(ctx);
    try {
        server.bind('inproc://example-pair-callback');
        client.connect('inproc://example-pair-callback');
        const received = await new Promise((resolve, reject) => {
            try {
                server.recvHandler((routingId, parts) => {
                    resolve({ routingId, parts });
                });
            }
            catch (error) {
                reject(error);
                return;
            }
            client.send([zlink.Message.copyOf('left'), zlink.Message.copyOf('right')]);
        });
        assert.equal(received.routingId, null);
        assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['left', 'right']);
        console.log('pair callback sample ok');
    }
    finally {
        client.close();
        server.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
