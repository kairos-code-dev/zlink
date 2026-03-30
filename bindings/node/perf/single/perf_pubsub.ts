// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const { createPayload, latencyUsFromPayload, stampPayload } = require('../common/perf_metrics');

async function runPubSubBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);
  const endpoint = `inproc://perf-pubsub-${process.pid}-${msgSize}`;
  const topic = 'perf:pubsub';
  const payload = createPayload(msgSize);
  const latenciesUs = [];
  let done = false;
  const startedAt = process.hrtime.bigint();
  const warmupNs = BigInt(Math.floor(options.warmup * 1_000_000_000));
  const activeNs = BigInt(Math.floor(options.duration * 1_000_000_000));

  try {
    pub.bind(endpoint);
    sub.connect(endpoint);
    sub.setSubscription(topic);

    sub.subscribeHandler((_, __, parts) => {
      const elapsed = process.hrtime.bigint() - startedAt;
      if (elapsed >= warmupNs && elapsed < warmupNs + activeNs) {
        latenciesUs.push(latencyUsFromPayload(parts[0].toBuffer()));
      }
      if (elapsed >= warmupNs + activeNs) {
        done = true;
      }
    });

    while (!done) {
      stampPayload(payload);
      const result = pub.tryPublish(topic, payload);
      if (result !== zlink.SendResult.Sent) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    return latenciesUs;
  } finally {
    sub.close();
    pub.close();
    ctx.close();
  }
}

module.exports = { runPubSubBenchmark };
