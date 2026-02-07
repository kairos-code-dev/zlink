'use strict';

const test = require('node:test');
const assert = require('node:assert');
const {
  zlink,
  transports,
  endpointFor,
  tryTransport,
  recvWithTimeout,
  gatewaySendWithRetry,
  spotRecvWithTimeout,
  waitUntil,
} = require('./helpers');

test('discovery/gateway/spot: flow across transports', async () => {
  const ctx = new zlink.Context();
  const cases = await transports('discovery');
  for (const tc of cases) {
    await tryTransport(tc.name, async () => {
      const registry = new zlink.Registry(ctx);
      const regPub = `inproc://reg-pub-${Date.now()}`;
      const regRouter = `inproc://reg-router-${Date.now()}`;
      registry.setEndpoints(regPub, regRouter);
      registry.start();

      const discovery = new zlink.Discovery(ctx, zlink.SERVICE_TYPE_GATEWAY);
      discovery.connectRegistry(regPub);
      discovery.subscribe('svc');

      const receiver = new zlink.Receiver(ctx);
      const serviceEp = await endpointFor(tc.name, tc.endpoint, '-svc');
      receiver.bind(serviceEp);
      const receiverRouter = receiver.routerSocket();
      receiver.connectRegistry(regRouter);
      receiver.register('svc', serviceEp, 1);
      let status = -1;
      for (let i = 0; i < 20; i += 1) {
        const res = receiver.registerResult('svc');
        status = res.status;
        if (status === 0) break;
        await new Promise(r => setTimeout(r, 50));
      }
      assert.strictEqual(status, 0);
      assert.equal(await waitUntil(() => discovery.receiverCount('svc') > 0, 5000), true);

      const gateway = new zlink.Gateway(ctx, discovery);
      assert.equal(await waitUntil(() => gateway.connectionCount('svc') > 0, 5000), true);
      await gatewaySendWithRetry(gateway, 'svc', [Buffer.from('hello')], 0, 5000);

      const rid = await recvWithTimeout(receiverRouter, 256, 2000);
      let payload = null;
      for (let i = 0; i < 3; i += 1) {
        payload = await recvWithTimeout(receiverRouter, 256, 2000);
        if (payload.toString().trim() === 'hello') {
          break;
        }
      }
      assert.strictEqual(payload.toString().trim(), 'hello');

      const node = new zlink.SpotNode(ctx);
      const spotEp = await endpointFor(tc.name, tc.endpoint, '-spot');
      node.bind(spotEp);
      node.connectRegistry(regRouter);
      node.register('spot', spotEp);
      await new Promise(r => setTimeout(r, 100));

      const peer = new zlink.SpotNode(ctx);
      peer.connectRegistry(regRouter);
      peer.connectPeerPub(spotEp);
      const spot = new zlink.Spot(peer);
      await new Promise(r => setTimeout(r, 100));
      spot.subscribe('topic');
      spot.publish('topic', [Buffer.from('spot-msg')], 0);

      const spotMsg = await spotRecvWithTimeout(spot, 5000);
      assert.strictEqual(spotMsg.topic, 'topic');
      assert.strictEqual(spotMsg.parts.length, 1);
      assert.strictEqual(spotMsg.parts[0].toString(), 'spot-msg');

      spot.close();
      peer.close();
      node.close();
      gateway.close();
      receiverRouter.close();
      receiver.close();
      discovery.close();
      registry.close();
    });
  }
  ctx.close();
});
