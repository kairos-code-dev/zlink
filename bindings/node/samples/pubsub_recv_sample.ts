// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist');

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
    const pubMonitor = pub.monitorOpen(zlink.MonitorEvent.CONNECTION_READY_CHANGED);
    const subMonitor = sub.monitorOpen(zlink.MonitorEvent.CONNECTION_READY_CHANGED);
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
    pub.publish(topic, zlink.Message.copyOf(sent));

    const received = sub.subscribe();
    const recv = received.parts[0].toBuffer().toString();
    assert.equal(received.topic, topic);
    assert.equal(recv, sent);
    console.log(`[pubsub/recv] publish: "${topic}/${sent}" \u2192 subscribe: "${topic}/${recv}"`);
  } finally {
    sub.close();
    pub.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
