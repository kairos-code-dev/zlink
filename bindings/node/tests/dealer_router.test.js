'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../src');

test('dealer/router uses routing id through Received and sendParts', () => {
  const ctx = new zlink.Context();
  const router = new zlink.Socket(ctx, zlink.SocketType.ROUTER);
  const dealer = new zlink.Socket(ctx, zlink.SocketType.DEALER);

  router.bind('inproc://dealer-router-contract');
  dealer.connect('inproc://dealer-router-contract');

  dealer.send(zlink.Message.copyOf('hello'));
  const request = router.recv();

  assert.equal(request.parts.length, 1);
  assert.equal(request.parts[0].toString(), 'hello');
  assert.ok(Buffer.isBuffer(request.routingId));

  router.sendParts([
    zlink.Message.wrap(request.routingId),
    zlink.Message.copyOf('world')
  ]);

  const response = dealer.recv();
  assert.equal(response.parts.length, 1);
  assert.equal(response.parts[0].toString(), 'world');

  dealer.close();
  router.close();
  ctx.close();
});
