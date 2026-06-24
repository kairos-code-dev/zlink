// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
const { tcpEndpoint } = require('./sample_support');
const REQUEST_PAYLOAD = 'spot-ping';
const REPLY_PAYLOAD = 'spot-pong';
const CHANNEL_NAME = 'orders';
async function recvRouterRequest(router, received, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        if (router.recv(received, zlink.RecvFlags.DontWait)) {
            return;
        }
        await new Promise((resolve) => setTimeout(resolve, 1));
    }
    throw new Error('router request receive timed out');
}
async function main() {
    const endpoint = await tcpEndpoint();
    const ctx = zlink.createContext();
    const requesterNode = zlink.createSpotNode(ctx);
    const responderRouter = zlink.createRouterSocket(ctx);
    const requesterRouter = zlink.createRouterSocket(ctx);
    const bridge = requesterNode.createRouteBridge();
    let requester = null;
    try {
        const requesterRid = zlink.RoutingId.from(Buffer.from('spot-request-client', 'ascii'));
        const responderRid = zlink.RoutingId.from(Buffer.from('spot-request-server', 'ascii'));
        requesterRouter.setRoutingId(requesterRid);
        responderRouter.setRoutingId(responderRid);
        requester = requesterNode.createSpot();
        responderRouter.bind(endpoint);
        requesterRouter.connect(endpoint);
        bridge.attachRouterChannel(CHANNEL_NAME, requesterRouter);
        const pendingReply = bridge.request(CHANNEL_NAME, responderRid, requester.routingId)
            .message(Buffer.from(REQUEST_PAYLOAD))
            .timeout(2000)
            .submit();
        const received = new zlink.Received();
        await recvRouterRequest(responderRouter, received, 2000);
        try {
            assert.ok(received.routingId);
            assert.notEqual(received.requestSeq, null);
            assert.equal(received.parts.at(-1).data().toString(), REQUEST_PAYLOAD);
            responderRouter.reply(received.routingId, received.requestSeq)
                .message(Buffer.from(REPLY_PAYLOAD))
                .submit();
        }
        finally {
            received.close();
        }
        const reply = await pendingReply;
        try {
            assert.equal(reply.length, 1);
            assert.equal(reply[0].data().toString(), REPLY_PAYLOAD);
        }
        finally {
            for (const part of reply) {
                part.close();
            }
        }
        console.log(`[spot/request] request: "${REQUEST_PAYLOAD}" -> reply: "${REPLY_PAYLOAD}"`);
    }
    finally {
        if (requester) {
            requester.close();
        }
        bridge.close();
        requesterRouter.close();
        responderRouter.close();
        requesterNode.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
