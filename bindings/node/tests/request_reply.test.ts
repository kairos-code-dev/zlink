'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist/canonical');

test('request-reply helpers expose canonical socket accessors', () => {
  const ctx = new zlink.Context();
  const routerSocket = new zlink.RouterSocket(ctx);
  const dealerSocket = new zlink.DealerSocket(ctx);
  const router = new zlink.RequestRouter(routerSocket);
  const dealer = new zlink.RequestDealer(dealerSocket);

  assert.equal(router.socket(), routerSocket);
  assert.equal(dealer.socket(), dealerSocket);
  assert.equal(typeof router.request, 'function');
  assert.equal(typeof router.reply, 'function');
  assert.equal(typeof router.recv, 'function');
  assert.equal(typeof router.onReceive, 'function');
  assert.equal(typeof dealer.request, 'function');
  assert.equal(typeof dealer.recv, 'function');
  assert.equal(typeof dealer.onReceive, 'function');

  dealer.close();
  router.close();
  ctx.close();
});

test('router recv and reply still work through the canonical socket surface', () => {
  const ctx = new zlink.Context();
  const routerSocket = new zlink.RouterSocket(ctx);
  const dealerSocket = new zlink.DealerSocket(ctx);

  routerSocket.bind('inproc://request-reply-contract');
  dealerSocket.connect('inproc://request-reply-contract');

  dealerSocket.send('ping');
  const request = routerSocket.recv();
  assert.ok(Buffer.isBuffer(request.routingId));
  assert.equal(request.requestSeq, null);
  assert.equal(request.parts[0].data().toString(), 'ping');
  routerSocket.send(request.routingId, 'pong');

  const reply = dealerSocket.recv();
  assert.equal(reply.parts[0].data().toString(), 'pong');
  assert.equal(reply.requestSeq, null);

  dealerSocket.close();
  routerSocket.close();
  ctx.close();
});
