// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  summarizeMetrics
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { drainRecvSocket, waitForConnectionReady } = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const dealers = [];
  let collector = null;
  let stop = false;

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const dealer = new zlink.DealerSocket(ctx);
      dealers.push(dealer);
    }
    for (const dealer of dealers) {
      await waitForConnectionReady(dealer, () => dealer.connect(options.endpoint));
    }

    const recvTasks = dealers.map((dealer) => drainRecvSocket(
      dealer,
      (received) => {
        if (!collector) {
          return;
        }
        collector.record(
          decodeMetricHeader(received.parts[0].data()),
          currentEpochNs()
        );
      },
      () => stop
    ));

    console.log(`CLIENT_READY,${options.msgSize}`);
    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    let active = false;
    for await (const line of rl) {
      if (line === `PHASE_ACTIVE,${options.msgSize}`) {
        active = true;
        break;
      }
    }
    if (!active) {
      throw new Error('dealer-dealer client missing PHASE_ACTIVE');
    }

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    collector = createMetricCollector({
      runId: createRunId(1),
      msgSize: options.msgSize,
      activeStartNs,
      activeStopNs
    });
    while (currentEpochNs() < activeStopNs + 250_000_000n) {
      await new Promise((resolve) => setImmediate(resolve));
    }
    stop = true;

    await Promise.all(recvTasks);
    const result = collector ? await collector.finish() : { latenciesNs: [] };
    for (const line of summarizeMetrics(
      'MULTI_DEALER_DEALER',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration
    )) {
      console.log(line);
    }
  } finally {
    for (const dealer of dealers) {
      dealer.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
