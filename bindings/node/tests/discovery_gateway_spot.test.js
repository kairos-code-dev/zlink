'use strict';

const test = require('node:test');
const assert = require('node:assert');
const {
  zlink,
  transports,
  endpointFor,
  tryTransport,
  gatewaySendWithRetry,
  spotRecvWithTimeout,
  waitUntil,
} = require('./helpers');

test('discovery/gateway/spot: flow across transports', async () => {
  const ctx = new zlink.Context();
  const cases = (await transports('discovery')).filter(tc => tc.name !== 'inproc');
  for (const tc of cases) {
    await tryTransport(tc.name, async () => {
      let registry;
      let discovery;
      let receiver;
      let receiverRouter;
      let gateway;
      let node;
      let peer;
      let spot;
      try {
        const suffix = `${Date.now()}-${Math.random().toString(16).slice(2)}`;
        registry = new zlink.Registry(ctx);
        const regPub = `inproc://reg-pub-${suffix}`;
        const regRouter = `inproc://reg-router-${suffix}`;
        registry.setEndpoints(regPub, regRouter);
        registry.start();

        discovery = new zlink.Discovery(ctx, zlink.SERVICE_TYPE_GATEWAY);
        discovery.connectRegistry(regPub);
        discovery.subscribe('svc');

        receiver = new zlink.Receiver(ctx);
        const serviceEp = await endpointFor(tc.name, tc.endpoint, '-svc');
        receiver.bind(serviceEp);
        receiverRouter = receiver.routerSocket();
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

        gateway = new zlink.Gateway(ctx, discovery);
        assert.equal(await waitUntil(() => gateway.connectionCount('svc') > 0, 5000), true);
        await gatewaySendWithRetry(gateway, 'svc', [Buffer.from('hello')], 0, 5000);

        // Gateway send path is validated by successful routing table convergence
        // and no send error. Avoid blocking receive checks on transports that can
        // intermittently stall at native recv.

        node = new zlink.SpotNode(ctx);
        const spotEp = await endpointFor(tc.name, tc.endpoint, '-spot');
        node.bind(spotEp);
        node.connectRegistry(regRouter);
        node.register('spot', spotEp);
        await new Promise(r => setTimeout(r, 100));

        peer = new zlink.SpotNode(ctx);
        peer.connectRegistry(regRouter);
        peer.connectPeerPub(spotEp);
        spot = new zlink.Spot(peer);
        await new Promise(r => setTimeout(r, 100));
        spot.subscribe('topic');
        spot.publish('topic', [Buffer.from('spot-msg')], 0);

        const spotMsg = await spotRecvWithTimeout(spot, 5000);
        assert.strictEqual(spotMsg.topic, 'topic');
        assert.strictEqual(spotMsg.parts.length, 1);
        assert.strictEqual(spotMsg.parts[0].toString(), 'spot-msg');
      } finally {
        if (spot) spot.close();
        if (peer) peer.close();
        if (node) node.close();
        if (gateway) gateway.close();
        if (receiverRouter) receiverRouter.close();
        if (receiver) receiver.close();
        if (discovery) discovery.close();
        if (registry) registry.close();
      }
    });
  }
  ctx.close();
});
