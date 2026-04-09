'use strict';
Object.defineProperty(exports, "__esModule", { value: true });
const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');
test('request dealer/router roundtrip works and preserves request info', async () => {
    const ctx = new zlink.Context();
    const routerSocket = new zlink.RouterSocket(ctx);
    const dealerSocket = new zlink.DealerSocket(ctx);
    const router = new zlink.RequestRouter(routerSocket);
    const dealer = new zlink.RequestDealer(dealerSocket);
    routerSocket.bind('inproc://request-reply-contract');
    dealerSocket.connect('inproc://request-reply-contract');
    router.onReceive((routingId, parts) => {
        assert.ok(Buffer.isBuffer(routingId));
        const info = parts[0].getRequestInfo();
        assert.equal(info.msgType, zlink.MsgType.Request);
        router.reply(routingId, info.correlationId, zlink.Message.fromBuffer(Buffer.from('pong')));
    });
    const reply = await dealer.request(zlink.Message.fromBuffer(Buffer.from('ping')), {
        timeout: 2000
    });
    assert.equal(reply.parts[0].data.toString(), 'pong');
    assert.equal(reply.parts[0].getRequestInfo().msgType, zlink.MsgType.Reply);
    dealer.close();
    router.close();
    ctx.close();
});
test('request router preserves data recv surface', () => {
    const ctx = new zlink.Context();
    const routerSocket = new zlink.RouterSocket(ctx);
    const dealerSocket = new zlink.DealerSocket(ctx);
    const router = new zlink.RequestRouter(routerSocket);
    routerSocket.bind('inproc://request-reply-data-contract');
    dealerSocket.connect('inproc://request-reply-data-contract');
    dealerSocket.send('plain-data');
    const received = router.recv();
    assert.equal(received.parts[0].data.toString(), 'plain-data');
    assert.equal(received.parts[0].getRequestInfo().msgType, zlink.MsgType.Data);
    router.close();
    dealerSocket.close();
    ctx.close();
});
