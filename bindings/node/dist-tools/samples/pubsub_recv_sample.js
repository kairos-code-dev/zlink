// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');
async function reservePort() {
    const srv = net.createServer();
    srv.listen(0, '127.0.0.1');
    await once(srv, 'listening');
    const { port } = srv.address();
    await new Promise((resolve, reject) => srv.close((error) => error ? reject(error) : resolve()));
    return port;
}
async function main() {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = new zlink.Context();
    const pub = new zlink.PubSocket(ctx);
    const sub = new zlink.SubSocket(ctx);
    try {
        const pubMonitor = pub.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
        const subMonitor = sub.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
        try {
            pub.bind(endpoint);
            sub.connect(endpoint);
            pubMonitor.recv();
            subMonitor.recv();
        }
        finally {
            pubMonitor.close();
            subMonitor.close();
        }
        const topic = 'prices';
        const sent = '101.25';
        sub.setSubscription(topic);
        const deadline = Date.now() + 5000;
        let received = null;
        while (Date.now() < deadline) {
            pub.publish(topic, Buffer.from(sent));
            try {
                received = sub.subscribe(zlink.RecvFlags.DontWait);
                if (received) {
                    break;
                }
            }
            catch (error) {
                if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
                    throw error;
                }
            }
            await new Promise((resolve) => setTimeout(resolve, 25));
        }
        assert.notEqual(received, null);
        try {
            const recv = received.parts[0].data().toString();
            assert.equal(received.topic, topic);
            assert.equal(recv, sent);
            console.log(`[pubsub/recv] publish: "${topic}/${sent}" \u2192 subscribe: "${topic}/${recv}"`);
        }
        finally {
            received.close();
        }
    }
    finally {
        sub.close();
        pub.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
