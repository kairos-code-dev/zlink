// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist/canonical');

async function reservePort() {
  const srv = net.createServer();
  srv.listen(0, '127.0.0.1');
  await once(srv, 'listening');
  const { port } = srv.address();
  await new Promise((resolve, reject) => srv.close((error) => error ? reject(error) : resolve()));
  return port;
}

async function main() {
  const port = await reservePort();
  const endpoint = `tcp://127.0.0.1:${port}`;
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);

  try {
    const pubMonitor = pub.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    const subMonitor = sub.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
    try {
      pub.bind(endpoint);
      sub.connect(endpoint);
      pubMonitor.recv();
      subMonitor.recv();
    } finally {
      pubMonitor.close();
      subMonitor.close();
    }

    const topic = 'prices';
    const sent = '101.25';
    sub.setSubscription(topic);

    const receivedPromise = new Promise((resolve, reject) => {
      try {
        sub.onSubscribe((message) => {
          resolve(message);
        });
      } catch (error) {
        reject(error);
        return;
      }
    });
    const timeoutPromise = new Promise((_, reject) => {
      setTimeout(() => reject(new Error('pubsub callback sample timed out')), 5000);
    });
    const publishTimer = setInterval(() => {
      pub.publish(topic, Buffer.from(sent));
    }, 25);
    const received = await Promise.race([receivedPromise, timeoutPromise]).finally(() => {
      clearInterval(publishTimer);
    });

    assert.ok(received.routingId instanceof zlink.RoutingId);
    assert.equal(received.topic, topic);
    const recv = received.parts[0].data().toString();
    assert.equal(recv, sent);
    console.log(`[pubsub/callback] publish: "${topic}/${sent}" \u2192 subscribe: "${topic}/${recv}"`);
  } finally {
    sub.close();
    pub.close();
    ctx.close();
  }

  process.exit(0);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
