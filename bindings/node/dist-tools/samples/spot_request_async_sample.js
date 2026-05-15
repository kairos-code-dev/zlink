// SPDX-License-Identifier: MPL-2.0
'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');
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
    const endpoint = `inproc://spot-request-async-${process.pid}`;
    const ctx = new zlink.Context();
    const requesterNode = new zlink.SpotNode(ctx);
    const responderRouter = new zlink.RouterSocket(ctx);
    const requesterDealer = new zlink.DealerSocket(ctx);
    let requester = null;
    try {
        requester = requesterNode.createSpot();
        responderRouter.bind(endpoint);
        requesterDealer.connect(endpoint);
        requesterNode.attachChannelDealerManual(CHANNEL_NAME, requesterDealer);
        const pendingReply = requester.requestChannel(CHANNEL_NAME)
            .message(Buffer.from(REQUEST_PAYLOAD))
            .timeout(2000)
            .submitAsync();
        const received = new zlink.Received();
        await recvRouterRequest(responderRouter, received, 2000);
        try {
            assert.ok(received.routingId);
            assert.notEqual(received.requestSeq, null);
            assert.equal(received.parts.length, 1);
            assert.equal(received.parts[0].data().toString(), REQUEST_PAYLOAD);
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
        console.log(`[spot/request/async] request: "${REQUEST_PAYLOAD}" -> reply: "${REPLY_PAYLOAD}"`);
    }
    finally {
        if (requester) {
            requester.close();
        }
        requesterDealer.close();
        responderRouter.close();
        requesterNode.close();
        ctx.close();
    }
}
main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
});
