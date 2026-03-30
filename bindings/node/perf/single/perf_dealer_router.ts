// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist');
const { createPayload, latencyUsFromPayload, stampPayload } = require('../common/perf_metrics');

async function runDealerRouterBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);
  const endpoint = `inproc://perf-dealer-router-${process.pid}-${msgSize}`;
  const payload = createPayload(msgSize);
  const latenciesUs = [];
  let done = false;
  const startedAt = process.hrtime.bigint();
  const warmupNs = BigInt(Math.floor(options.warmup * 1_000_000_000));
  const activeNs = BigInt(Math.floor(options.duration * 1_000_000_000));

  try {
    router.bind(endpoint);
    dealer.connect(endpoint);

    router.recvHandler((_, parts) => {
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
      const result = dealer.trySend(payload);
      if (result !== zlink.SendResult.Sent) {
        await new Promise((resolve) => setImmediate(resolve));
        continue;
      }
      await new Promise((resolve) => setImmediate(resolve));
    }

    return latenciesUs;
  } finally {
    dealer.close();
    router.close();
    ctx.close();
  }
}

module.exports = { runDealerRouterBenchmark };
