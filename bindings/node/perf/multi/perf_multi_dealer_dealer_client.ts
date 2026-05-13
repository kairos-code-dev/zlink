// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  summarizeMetrics
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  drainRecvSocket,
  resolveMultiLatencySampleCap,
  waitForConnectionReady
} = require('./perf_multi_runtime');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_DEALER_DEALER');
  const dealers = [];
  let rl = null;
  let collector = null;

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const dealer = new zlink.DealerSocket(ctx);
      applySocketPolicy(dealer);
      dealers.push(dealer);
    }
    for (const dealer of dealers) {
      await waitForConnectionReady(dealer, () => dealer.connect(options.endpoint));
      applyAutoHwmMsgUnit(dealer, options.msgSize);
    }
    ctx.recalculateAutoHwm();

    // PERF_MULTI_TEST_POLICY § 1.3.1: each receiver drains until it sees
    // the wire-level stop token emitted by the server when its duration
    // elapses. No time-based `stop` flag is needed — phase end is observed
    // on the wire.
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
      }
    ));

    console.log(`CLIENT_READY,${options.msgSize}`);
    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
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
      activeStopNs,
      sampleCap: resolveMultiLatencySampleCap()
    });
    await Promise.all(recvTasks);
    const result = collector ? await collector.finish() : { latenciesNs: [] };
    for (const line of summarizeMetrics(
      'MULTI_DEALER_DEALER',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      'current',
      result.accepted
    )) {
      console.log(line);
    }
  } finally {
    rl?.close();
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
