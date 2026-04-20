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
const {
  applySocketPolicy,
  drainRecvSocket,
  waitForConnectionReady
} = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const dealers = [];
  let collector = null;
  let stop = false;

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const dealer = new zlink.DealerSocket(ctx);
      applySocketPolicy(dealer);
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
    for await (const line of rl) {
      if (line === `START,${options.msgSize}`) {
        break;
      }
    }

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    collector = createMetricCollector({
      runId: createRunId(1),
      msgSize: options.msgSize,
      activeStartNs,
      activeStopNs
    });
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
