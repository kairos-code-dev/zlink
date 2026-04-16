// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');

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
  const endpoint = `tcp://127.0.0.1:${await reservePort()}`;
  const ctx = new zlink.Context();
  const node = new zlink.SpotNode(ctx);
  const pubSocket = new zlink.PubSocket(ctx);
  const subSocket = new zlink.SubSocket(ctx);
  let spot = null;
  const topic = 'room:lobby';
  const sent = 'hello-spot';

  try {
    pubSocket.bind(endpoint);
    subSocket.connect(endpoint);
    node.attachPubSub(SERVICE_NAME, pubSocket, subSocket);
    spot = node.createSpot();
    const delivered = new Promise<{ serviceName: string; topic: string; payload: string }>((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('spot dispatch callback did not deliver within 5s')), 5000);
      spot.onDispatchEvent((event) => {
        if (event !== zlink.SpotDispatchEvent.SubscribeReadable) {
          return;
        }
        try {
          const received = spot.subscribe(zlink.RecvFlags.DontWait);
          clearTimeout(timeout);
          resolve({
            serviceName: received.serviceName ?? '',
            topic: received.topic,
            payload: received.parts[0].data().toString(),
          });
        } catch (error) {
          clearTimeout(timeout);
          reject(error);
        }
      });
    });
    spot.setSubscription(topic);
    spot.publish(SERVICE_NAME, topic, Buffer.from(sent));
    const received = await delivered;
    assert.equal(received.topic, topic);
    assert.equal(received.serviceName, SERVICE_NAME);
    assert.equal(received.payload, sent);
    console.log(`[spot/recv] service: "${SERVICE_NAME}" tick: 1 publish: "${topic}/${sent}" -> recv: "${topic}/${received.payload}"`);
  } finally {
    if (spot) {
      spot.close();
    }
    node.close();
    subSocket.close();
    pubSocket.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
