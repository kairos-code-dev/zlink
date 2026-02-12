'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { zlink, waitUntil, ZLINK_DEALER, ZLINK_PAIR, ZLINK_ROUTER } = require('./helpers');

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

test('fastpath: pair sendMany/recvManyInto works', async () => {
  const ctx = new zlink.Context();
  const a = new zlink.Socket(ctx, ZLINK_PAIR);
  const b = new zlink.Socket(ctx, ZLINK_PAIR);
  const endpoint = `inproc://fastpath-pair-${Date.now()}`;

  try {
    a.bind(endpoint);
    b.connect(endpoint);
    await sleep(50);

    const payload = Buffer.from('fastpath-payload');
    const recvBuf = Buffer.alloc(64);
    const count = 64;

    assert.strictEqual(b.sendMany(payload, count, zlink.SendFlag.NONE), count);
    assert.strictEqual(a.recvManyInto(recvBuf, count, zlink.ReceiveFlag.NONE), count);
    assert.strictEqual(
      recvBuf.subarray(0, payload.length).toString('utf8'),
      payload.toString('utf8')
    );
  } finally {
    a.close();
    b.close();
    ctx.close();
  }
});

test('fastpath: dealer-router recvPairDrainInto drains queued messages', async () => {
  const ctx = new zlink.Context();
  const router = new zlink.Socket(ctx, ZLINK_ROUTER);
  const dealer = new zlink.Socket(ctx, ZLINK_DEALER);
  const endpoint = `inproc://fastpath-drain-${Date.now()}`;

  try {
    dealer.setSockOpt(zlink.SocketOption.ROUTING_ID, Buffer.from('CLIENT'));
    router.bind(endpoint);
    dealer.connect(endpoint);
    await sleep(50);

    const payload = Buffer.alloc(16, 'a');
    const ridBuf = Buffer.alloc(256);
    const dataBuf = Buffer.alloc(64);
    const count = 128;

    assert.strictEqual(dealer.sendMany(payload, count, zlink.SendFlag.NONE), count);

    let received = 0;
    const deadline = Date.now() + 3000;
    while (received < count && Date.now() < deadline) {
      const drained = router.recvPairDrainInto(ridBuf, dataBuf, count - received);
      if (drained === 0) {
        await sleep(5);
        continue;
      }
      received += drained;
    }
    assert.strictEqual(received, count);
    assert.strictEqual(
      dataBuf.subarray(0, payload.length).toString('utf8'),
      payload.toString('utf8')
    );
  } finally {
    router.close();
    dealer.close();
    ctx.close();
  }
});

test('fastpath: router-router sendRoutedMany/recvPairManyInto works', async () => {
  const ctx = new zlink.Context();
  const router1 = new zlink.Socket(ctx, ZLINK_ROUTER);
  const router2 = new zlink.Socket(ctx, ZLINK_ROUTER);
  const endpoint = `inproc://fastpath-router-${Date.now()}`;

  try {
    const rid1 = Buffer.from('ROUTER1');
    const rid2 = Buffer.from('ROUTER2');
    router1.setSockOpt(zlink.SocketOption.ROUTING_ID, rid1);
    router2.setSockOpt(zlink.SocketOption.ROUTING_ID, rid2);
    router1.bind(endpoint);
    router2.connect(endpoint);
    await sleep(100);

    const payload = Buffer.alloc(20, 'b');
    const ridBuf = Buffer.alloc(256);
    const dataBuf = Buffer.alloc(64);
    const count = 64;

    let sent = false;
    const deadline = Date.now() + 3000;
    while (!sent && Date.now() < deadline) {
      try {
        router2.sendRoutedMany(rid1, rid1.length, payload, payload.length, count, zlink.SendFlag.NONE);
        sent = true;
      } catch (_) {
        await sleep(10);
      }
    }
    assert.strictEqual(sent, true);
    assert.strictEqual(
      router1.recvPairManyInto(ridBuf, dataBuf, count, zlink.ReceiveFlag.NONE),
      count
    );
    assert.strictEqual(
      dataBuf.subarray(0, payload.length).toString('utf8'),
      payload.toString('utf8')
    );
  } finally {
    router1.close();
    router2.close();
    ctx.close();
  }
});

test('fastpath: gateway sendManyConst works', async () => {
  const ctx = new zlink.Context();
  let registry;
  let discovery;
  let receiver;
  let router;
  let gateway;
  const suffix = `${Date.now()}-${Math.random().toString(16).slice(2)}`;

  try {
    const regPub = `inproc://fastpath-gw-pub-${suffix}`;
    const regRouter = `inproc://fastpath-gw-router-${suffix}`;
    const providerEp = `inproc://fastpath-gw-provider-${suffix}`;
    registry = new zlink.Registry(ctx);
    registry.setHeartbeat(5000, 60000);
    registry.setEndpoints(regPub, regRouter);
    registry.start();

    discovery = new zlink.Discovery(ctx, zlink.ServiceType.GATEWAY);
    discovery.connectRegistry(regPub);
    discovery.subscribe('svc');

    receiver = new zlink.Receiver(ctx);
    receiver.bind(providerEp);
    receiver.connectRegistry(regRouter);
    receiver.register('svc', providerEp, 1);
    router = receiver.routerSocket();

    gateway = new zlink.Gateway(ctx, discovery);
    assert.equal(await waitUntil(() => discovery.receiverCount('svc') > 0, 5000), true);
    assert.equal(await waitUntil(() => gateway.connectionCount('svc') > 0, 5000), true);
    await sleep(100);

    const payload = Buffer.from('gateway-fastpath');
    const ridBuf = Buffer.alloc(256);
    const dataBuf = Buffer.alloc(64);
    const count = 64;

    assert.strictEqual(
      gateway.sendManyConst('svc', payload, count, zlink.SendFlag.NONE),
      count
    );
    assert.strictEqual(
      router.recvPairManyInto(ridBuf, dataBuf, count, zlink.ReceiveFlag.NONE),
      count
    );
    assert.strictEqual(
      dataBuf.subarray(0, payload.length).toString('utf8'),
      payload.toString('utf8')
    );
  } finally {
    if (gateway) gateway.close();
    if (router) router.close();
    if (receiver) receiver.close();
    if (discovery) discovery.close();
    if (registry) registry.close();
    ctx.close();
  }
});

test('fastpath: spot publishManyConst/recvMany works', async () => {
  const ctx = new zlink.Context();
  const nodePub = new zlink.SpotNode(ctx);
  const nodeSub = new zlink.SpotNode(ctx);
  const endpoint = `inproc://fastpath-spot-${Date.now()}`;
  let spotPub;
  let spotSub;

  try {
    nodePub.bind(endpoint);
    nodeSub.connectPeerPub(endpoint);
    spotPub = new zlink.Spot(nodePub);
    spotSub = new zlink.Spot(nodeSub);
    spotSub.subscribe('bench');
    await sleep(200);

    const payload = Buffer.from('spot-fastpath');
    const parts = [payload];
    const count = 64;

    assert.strictEqual(
      spotPub.publishManyConst('bench', payload, count, zlink.SendFlag.NONE),
      count
    );
    assert.strictEqual(
      spotSub.recvMany(count, zlink.ReceiveFlag.NONE),
      count
    );

    spotPub.publish('bench', parts, zlink.SendFlag.NONE);
    const msg = spotSub.recv(zlink.ReceiveFlag.NONE);
    assert.strictEqual(msg.topic, 'bench');
    assert.strictEqual(msg.parts.length, 1);
    assert.strictEqual(msg.parts[0].toString('utf8'), payload.toString('utf8'));
  } finally {
    if (spotSub) spotSub.close();
    if (spotPub) spotPub.close();
    nodeSub.close();
    nodePub.close();
    ctx.close();
  }
});
