// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist');
async function reservePort() {
    const srv = net.createServer();
    srv.listen(0, '127.0.0.1');
    await once(srv, 'listening');
    const { port } = srv.address();
    await new Promise((resolve, reject) => srv.close((error) => error ? reject(error) : resolve()));
    return port;
}
function waitFor(label, ms) {
    let resolve;
    let reject;
    const promise = new Promise((res, rej) => {
        resolve = res;
        reject = rej;
    });
    const timer = setTimeout(() => reject(new Error(`${label} timed out`)), ms);
    return {
        promise: promise.finally(() => clearTimeout(timer)),
        resolve
    };
}
async function main() {
    const port = await reservePort();
    const endpoint = `tcp://127.0.0.1:${port}`;
    const ctx = new zlink.Context();
    const routerSocket = new zlink.RouterSocket(ctx);
    const dealerSocket = new zlink.DealerSocket(ctx);
    const router = new zlink.RequestRouter(routerSocket);
    const dealer = new zlink.RequestDealer(dealerSocket);
    try {
        const routerMonitor = routerSocket.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
        const dealerMonitor = dealerSocket.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
        try {
            dealerSocket.setRoutingId(Buffer.from('request-reply-client'));
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);
            routerMonitor.recv();
            dealerMonitor.recv();
        }
        finally {
            routerMonitor.close();
            dealerMonitor.close();
        }
        const replyHandled = waitFor('reply callback', 2000);
        const requestHandled = waitFor('request callback', 2000);
        router.onReceive((received) => {
            assert.equal(Buffer.from(received.routingId).toString(), 'request-reply-client');
            assert.ok(typeof received.requestSeq === 'bigint');
            router.reply(received.routingId, received.requestSeq, zlink.Message.fromBuffer(Buffer.from('pong')));
            requestHandled.resolve();
        });
        dealer.request(zlink.Message.fromBuffer(Buffer.from('ping')), (error, reply) => {
            assert.ifError(error);
            assert.equal(reply.parts[0].data.toString(), 'pong');
            replyHandled.resolve();
        }, { timeout: 2000 });
        await requestHandled.promise;
        await replyHandled.promise;
        console.log('[dealer-router/request-reply/callback] send: "ping" -> recv: "pong"');
    }
    finally {
        dealer.close();
        router.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
