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
function timeoutPromise(ms, label) {
    return new Promise((_, reject) => {
        setTimeout(() => reject(new Error(`${label} timed out`)), ms);
    });
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
        const pendingReply = dealer.request(zlink.Message.fromBuffer(Buffer.from('ping')), { timeoutMs: 2000 });
        const request = router.recv();
        assert.equal(Buffer.from(request.routingId).toString(), 'request-reply-client');
        const info = request.parts[0].getRequestInfo();
        assert.equal(info.msgType, zlink.MsgType.Request);
        router.reply(request.routingId, info.correlationId, zlink.Message.fromBuffer(Buffer.from('pong')));
        const reply = await pendingReply;
        assert.equal(reply.parts[0].data.toString(), 'pong');
        console.log('[dealer-router/request-reply/async] send: "ping" -> recv: "pong"');
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
