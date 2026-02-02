'use strict';

const test = require('node:test');
const assert = require('node:assert');
const {
  zlink,
  transports,
  endpointFor,
  tryTransport,
  recvWithTimeout,
  sendWithRetry,
  gatewayRecvWithTimeout,
  spotRecvWithTimeout,
  ZLINK_ROUTER,
  ZLINK_SNDMORE,
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

      const discovery = new zlink.Discovery(ctx);
      discovery.connectRegistry(regPub);
      discovery.subscribe('svc');

      const provider = new zlink.Provider(ctx);
      const serviceEp = await endpointFor(tc.name, tc.endpoint, '-svc');
      provider.bind(serviceEp);
      const providerRouter = provider.routerSocket();
      provider.connectRegistry(regRouter);
      provider.register('svc', serviceEp, 1);

      const gateway = new zlink.Gateway(ctx, discovery);
      gateway.send('svc', [Buffer.from('hello')], 0);

      const rid = await recvWithTimeout(providerRouter, 256, 2000);
      let payload = null;
      for (let i = 0; i < 3; i += 1) {
        payload = await recvWithTimeout(providerRouter, 256, 2000);
        if (payload.toString().trim() === 'hello') {
          break;
        }
      }
      assert.strictEqual(payload.toString().trim(), 'hello');
      providerRouter.send(rid, ZLINK_SNDMORE);
      await sendWithRetry(providerRouter, Buffer.from('world'), 0, 2000);

      const gwMsg = await gatewayRecvWithTimeout(gateway, 2000);
      assert.strictEqual(gwMsg.service, 'svc');
      assert.strictEqual(gwMsg.parts.length, 1);
      assert.strictEqual(gwMsg.parts[0].toString(), 'world');

      const node = new zlink.SpotNode(ctx);
      const spotEp = await endpointFor(tc.name, tc.endpoint, '-spot');
      node.bind(spotEp);
      node.connectRegistry(regRouter);
      node.register('spot', spotEp);

      const peer = new zlink.SpotNode(ctx);
      peer.connectRegistry(regRouter);
      peer.connectPeerPub(spotEp);
      const spot = new zlink.Spot(peer);
      spot.subscribe('topic');
      spot.publish('topic', [Buffer.from('spot-msg')], 0);

      const spotMsg = await spotRecvWithTimeout(spot, 2000);
      assert.strictEqual(spotMsg.topic, 'topic');
      assert.strictEqual(spotMsg.parts.length, 1);
      assert.strictEqual(spotMsg.parts[0].toString(), 'spot-msg');

      spot.close();
      peer.close();
      node.close();
      gateway.close();
      providerRouter.close();
      provider.close();
      discovery.close();
      registry.close();
    });
  }
  ctx.close();
});
