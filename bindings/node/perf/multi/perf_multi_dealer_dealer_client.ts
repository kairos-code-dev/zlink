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
const { configureTlsClient } = require('../common/perf_tls');
const { parseMultiArgs } = require('./perf_multi_common');
const {
  POLLIN,
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  emitMultiSocketHwmDetail,
  pollEvents,
  recvNoWait,
  resolveMultiLatencySampleCap,
  waitForConnectionReady
} = require('./perf_multi_runtime');
const { isStopTokenParts } = require('../perf_stop_token');

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
      configureTlsClient(dealer, options.transport);
      dealers.push(dealer);
    }
    for (const dealer of dealers) {
      await waitForConnectionReady(dealer, () => dealer.connect(options.endpoint));
      applyAutoHwmMsgUnit(dealer, options.msgSize);
    }
    ctx.recalculateAutoHwm();
    for (const dealer of dealers) {
      emitMultiSocketHwmDetail(dealer, 'endpoint', options.transport, options.msgSize);
    }

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

    const poller = new zlink.Poller();
    try {
      for (let i = 0; i < dealers.length; i += 1) {
        poller.add(dealers[i], pollEvents(POLLIN), i);
      }
      let stopReceived = false;
      while (!stopReceived) {
        const ready = poller.waitMany(poller.size, -1);
        for (const event of ready) {
          const index = event.tag ?? event.userData;
          if (!Number.isInteger(index)) {
            continue;
          }
          while (true) {
            const received = recvNoWait(dealers[index]);
            if (!received) {
              break;
            }
            try {
              if (isStopTokenParts(received.parts)) {
                stopReceived = true;
                break;
              }
              collector.record(
                decodeMetricHeader(received.parts[0].data()),
                currentEpochNs()
              );
            } finally {
              received.close();
            }
          }
        }
      }
    } finally {
      poller.close();
    }
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
