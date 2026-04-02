// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const { once } = require('node:events');
const net = require('node:net');
const zlink = require('../dist');

const FILTER_APPLIED = zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED;
const PUB_READY = zlink.ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED
  | zlink.ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED;

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
  const pubNode = new zlink.SpotNode(ctx);
  const subNode = new zlink.SpotNode(ctx);
  const pub = new zlink.Spot(pubNode);
  const sub = new zlink.Spot(subNode);
  const subMonitor = sub.monitorOpen(FILTER_APPLIED);
  const pubMonitor = pub.monitorOpen(PUB_READY);
  const topic = 'room:lobby';
  const sent = 'hello-spot';

  try {
    pubNode.bind(endpoint);
    subNode.connectPeer(endpoint);
    sub.setSubscription(topic);
    let filterApplied = false;
    const deadline = Date.now() + 5000;
    while (Date.now() < deadline) {
      const event = subMonitor.tryRecv();
      if (!event) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      if (event.eventType === FILTER_APPLIED) {
        filterApplied = true;
        break;
      }
    }
    assert.equal(filterApplied, true);
    let pubReady = false;
    while (Date.now() < deadline) {
      const event = pubMonitor.tryRecv();
      if (!event) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      if (
        (event.eventType === zlink.ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED
          || event.eventType === zlink.ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED)
        && event.value > 0
      ) {
        pubReady = true;
        break;
      }
    }
    assert.equal(pubReady, true);
    pub.publish(topic, zlink.Message.copyOf(sent));
    const received = sub.subscribe();
    assert.equal(received.topic, topic);
    const recv = received.parts[0].toBuffer().toString();
    assert.equal(recv, sent);
    console.log(`[spot/recv] publish: "${topic}/${sent}" \u2192 subscribe: "${topic}/${recv}"`);
  } finally {
    pubMonitor.close();
    subMonitor.close();
    sub.close();
    pub.close();
    subNode.close();
    pubNode.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
