'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const zlink = require('@zlink-systems/zlink');

test('dealer/router uses routing id through Received and routed send', () => {
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);

  router.bind('inproc://dealer-router-contract');
  dealer.connect('inproc://dealer-router-contract');

  dealer.send().message('hello').submit();
  const request = new zlink.Received();
  router.recv(request);

  assert.equal(request.parts.length, 1);
  assert.ok(Object.isFrozen(request.parts));
  assert.equal(request.parts[0].data().toString(), 'hello');
  assert.ok(request.routingId instanceof zlink.RoutingId);
  assert.equal(request.parts[0].refCount(), 1);
  assert.notEqual(request.parts[0].getProperty('Routing-Id'), null);
  assert.equal(request.parts[0].getProperty('Routing-Id'), request.parts[0].getProperty('Identity'));

  router.send(request.routingId).message('world').submit();

  const response = new zlink.Received();
  dealer.recv(response);
  assert.equal(response.parts.length, 1);
  assert.ok(Object.isFrozen(response.parts));
  assert.equal(response.parts[0].data().toString(), 'world');
  assert.equal(typeof router.reply, 'function');

  dealer.close();
  router.close();
  ctx.close();
});

test('router recvInto fills caller buffer and returns routing metadata', () => {
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);

  router.bind('inproc://dealer-router-recv-payload-into');
  dealer.connect('inproc://dealer-router-recv-payload-into');

  dealer.send().message(Buffer.from('payload')).submit();

  const buffer = Buffer.allocUnsafe(4);
  const received = router.recvInto(buffer);
  assert.equal(received?.size, 7);
  assert.ok(received?.routingId);
  assert.equal(received?.spotRid, null);
  assert.equal(received?.requestSeq, null);
  assert.equal(buffer.toString('utf8', 0, 4), 'payl');

  dealer.close();
  router.close();
  ctx.close();
});

test('router recvPayloadInto fills caller buffer without materializing Received', () => {
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);

  router.bind('inproc://dealer-router-recv-payload-into');
  dealer.connect('inproc://dealer-router-recv-payload-into');

  dealer.send().message(Buffer.from('payload')).submit();

  const buffer = Buffer.allocUnsafe(4);
  const size = router.recvPayloadInto(buffer);
  assert.equal(size, 7);
  assert.equal(buffer.toString('utf8', 0, 4), 'payl');

  dealer.close();
  router.close();
  ctx.close();
});
