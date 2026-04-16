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
    spot.setSubscription(topic);
    const deadline = Date.now() + 5000;
    let received = null;
    while (Date.now() < deadline) {
      spot.publish(SERVICE_NAME, topic, Buffer.from(sent));
      try {
        received = spot.subscribe(zlink.RecvFlags.DontWait);
        break;
      } catch (error) {
        if (!(error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData)) {
          throw error;
        }
      }
      await new Promise((resolve) => setTimeout(resolve, 25));
    }
    assert.notEqual(received, null);
    assert.equal(received.topic, topic);
    assert.equal(received.serviceName, SERVICE_NAME);
    const recv = received.parts[0].data().toString();
    assert.equal(recv, sent);
    console.log(`[spot/recv] service: "${SERVICE_NAME}" tick: 1 publish: "${topic}/${sent}" -> recv: "${topic}/${recv}"`);
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
