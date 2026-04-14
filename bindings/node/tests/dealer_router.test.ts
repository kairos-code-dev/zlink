'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('../dist/canonical');

test('dealer/router uses routing id through Received and routed send', () => {
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);

  router.bind('inproc://dealer-router-contract');
  dealer.connect('inproc://dealer-router-contract');

  dealer.send('hello');
  const request = router.recv();

  assert.equal(request.parts.length, 1);
  assert.ok(Object.isFrozen(request.parts));
  assert.equal(request.parts[0].data().toString(), 'hello');
  assert.ok(request.routingId instanceof zlink.RoutingId);
  assert.equal(request.parts[0].refCount(), 1);
  assert.notEqual(request.parts[0].getProperty('Routing-Id'), null);
  assert.equal(request.parts[0].getProperty('Routing-Id'), request.parts[0].getProperty('Identity'));

  router.send(request.routingId, ['world']);

  const response = dealer.recv();
  assert.equal(response.parts.length, 1);
  assert.ok(Object.isFrozen(response.parts));
  assert.equal(response.parts[0].data().toString(), 'world');
  assert.equal(typeof router.reply, 'function');

  dealer.close();
  router.close();
  ctx.close();
});
