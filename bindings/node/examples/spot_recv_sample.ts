// SPDX-License-Identifier: MPL-2.0

'use strict';

const assert = require('node:assert/strict');
const zlink = require('../dist');

const FILTER_APPLIED = zlink.ServiceMonitorEvent.SPOT_FILTER_APPLIED;

async function main() {
  const ctx = new zlink.Context();
  const spot = new zlink.Spot(ctx);
  const monitor = spot.openMonitor(FILTER_APPLIED);
  const topic = 'spot:sample';

  try {
    spot.setSubscription(topic);
    while (true) {
      const event = monitor.recv();
      if (event.eventType === FILTER_APPLIED) {
        break;
      }
    }
    spot.publish(topic, zlink.Message.copyOf('spot-recv'));
    await new Promise((resolve) => setImmediate(resolve));
    const received = spot.trySubscribe();
    assert.notEqual(received, null);
    assert.equal(received.topic, topic);
    assert.deepEqual(received.parts.map((part) => part.toBuffer().toString()), ['spot-recv']);
    console.log('spot recv sample ok');
  } finally {
    monitor.close();
    spot.close();
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
