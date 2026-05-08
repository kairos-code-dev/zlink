// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../..');
const AUTO_CONNECT_SPOT_MESH = 5;

const SERVICE_NAME = 'sample';

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const { port } = server.address();
  await new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function main() {
  const ctx = new zlink.Context();
  const registry = new zlink.Registry(ctx);
  const publisherDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, SERVICE_NAME);
  const subscriberDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, SERVICE_NAME);
  const publisherNode = new zlink.SpotNode(ctx);
  const subscriberNode = new zlink.SpotNode(ctx);
  let publisher = null;
  let subscriber = null;
  const topic = 'room:lobby';
  const sent = 'hello-spot';
  const registryPub = `tcp://127.0.0.1:${await reservePort()}`;
  const registryRouter = `tcp://127.0.0.1:${await reservePort()}`;
  const publisherEndpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const subscriberEndpoint = `tcp://127.0.0.1:${await reservePort()}`;

  try {
    registry.bind(registryPub, registryRouter);
    publisherDiscovery.connectRegistry(registryRouter);
    subscriberDiscovery.connectRegistry(registryRouter);
    publisherNode.attachDiscovery(publisherDiscovery);
    subscriberNode.attachDiscovery(subscriberDiscovery);
    publisherNode.bind(publisherEndpoint);
    subscriberNode.bind(subscriberEndpoint);
    publisher = publisherNode.createSpot();
    subscriber = subscriberNode.createSpot();
    subscriber.setSubscription(topic);
    const deadline = Date.now() + 15000;
    let received = null;
    while (Date.now() < deadline) {
      publisher.publish(SERVICE_NAME, topic).message(Buffer.from(sent)).submit();
      try {
        received = subscriber.subscribe(zlink.RecvFlags.DontWait);
        if (received) {
          break;
        }
      } catch (error) {
        if (!(error instanceof zlink.RecvError)) {
          throw error;
        }
        if (error.result !== zlink.RecvResult.NoData && error.internalErrno !== 2) {
          throw error;
        }
      }
      await new Promise((resolve) => setTimeout(resolve, 25));
    }
    if (!received) {
      console.log('[spot/recv] remote delivery not observed before timeout');
      return;
    }
    try {
      assert.equal(received.serviceName, SERVICE_NAME);
      assert.equal(received.topic, topic);
      const recv = received.parts[0].data().toString();
      assert.equal(recv, sent);
      console.log(`[spot/recv] service: "${SERVICE_NAME}" tick: 1 publish: "${topic}/${sent}" -> recv: "${topic}/${recv}"`);
    } finally {
      received.close();
    }
  } finally {
    if (subscriber) {
      subscriber.close();
    }
    if (publisher) {
      publisher.close();
    }
    subscriberNode.close();
    publisherNode.close();
    subscriberDiscovery.close();
    publisherDiscovery.close();
    registry.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
