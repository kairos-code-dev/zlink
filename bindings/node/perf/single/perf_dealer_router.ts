// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');
const {
  applySocketPolicy,
  benchmarkEndpoint,
  drainRecvSocket,
  parseSingleBinaryArgs,
  resolveSingleIdleDrainMs,
  waitForConnectionReady,
} = require('./perf_single_common');

async function runDealerRouterBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const router = new zlink.RouterSocket(ctx);
  const dealer = new zlink.DealerSocket(ctx);
  const endpoint = await benchmarkEndpoint(options.transport, `dealer-router-${msgSize}`);

  try {
    applySocketPolicy(router, options);
    applySocketPolicy(dealer, options);
    router.bind(endpoint);
    await waitForConnectionReady(dealer, () => dealer.connect(endpoint));

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    const runId = createRunId(options.runId ?? 1);
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs,
      sampleCap: Number(process.env.PERF_SINGLE_LATENCY_SAMPLE_CAP ?? 200000)
    });
    const payload = createPayload(msgSize);
    let seq = 1n;
    let stop = false;

    const recvTask = drainRecvSocket(
      router,
      (received) => {
        const header = decodeMetricHeaderFromParts(received.parts);
        collector.record(header, currentEpochNs());
      },
      () => stop
    );

    while (currentEpochNs() < activeStopNs) {
      for (let i = 0; i < 256 && currentEpochNs() < activeStopNs; i += 1) {
        stampPayload(payload, {
          phase: 1,
          runId,
          msgSize,
          seq
        });
        dealer.send(payload);
        seq += 1n;
      }
      if (currentEpochNs() < activeStopNs) {
        await sleepImmediate();
      }
    }
    stampPayload(payload, { phase: 2, runId, msgSize, seq });
    dealer.send(payload);
    const drainDeadlineNs = activeStopNs
      + BigInt(resolveSingleIdleDrainMs(options)) * 1_000_000n;
    while (currentEpochNs() < drainDeadlineNs) {
      await sleepImmediate();
    }
    stop = true;
    await recvTask;
    const result = await collector.finish();
    return result.latenciesNs;
  } finally {
    dealer.close();
    router.close();
    ctx.close();
  }
}

module.exports = { runDealerRouterBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const latenciesNs = await runDealerRouterBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'DEALER_ROUTER',
      options.transport,
      options.msgSize,
      latenciesNs,
      options.duration,
      options.libName
    )) {
      console.log(line);
    }
  })().catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
}
