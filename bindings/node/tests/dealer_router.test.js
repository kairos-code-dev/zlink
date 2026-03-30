'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist');

test('dealer/router uses routing id through Received and sendPartsTo', () => {
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);

  router.bind('inproc://dealer-router-contract');
  dealer.connect('inproc://dealer-router-contract');

  dealer.send(zlink.Message.copyOf('hello'));
  const request = router.recv();

  assert.equal(request.parts.length, 1);
  assert.equal(request.parts[0].toString(), 'hello');
  assert.ok(Buffer.isBuffer(request.routingId));

  router.sendPartsTo(request.routingId, [
    zlink.Message.copyOf('world')
  ]);

  const response = dealer.recv();
  assert.equal(response.parts.length, 1);
  assert.equal(response.parts[0].toString(), 'world');
  assert.throws(() => router.send(zlink.Message.copyOf('bad')), /sendTo/);
  assert.throws(() => router.sendParts([zlink.Message.copyOf('bad')]), /sendPartsTo/);

  dealer.close();
  router.close();
  ctx.close();
});
