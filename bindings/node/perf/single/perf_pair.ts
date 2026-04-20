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

async function runPairBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const server = new zlink.PairSocket(ctx);
  const client = new zlink.PairSocket(ctx);
  const endpoint = await benchmarkEndpoint(options.transport, `pair-${msgSize}`);

  try {
    applySocketPolicy(server, options);
    applySocketPolicy(client, options);
    server.bind(endpoint);
    await waitForConnectionReady(client, () => client.connect(endpoint));

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    const runId = createRunId(options.runId ?? 1);
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs
    });
    const payload = createPayload(msgSize);
    let seq = 1n;
    let stop = false;

    const recvTask = drainRecvSocket(
      server,
      (received) => {
        const header = decodeMetricHeaderFromParts(received.parts);
        collector.record(header, currentEpochNs());
      },
      () => stop
    );

    while (currentEpochNs() < activeStopNs) {
      stampPayload(payload, {
        phase: 1,
        runId,
        msgSize,
        seq
      });
      client.send(payload);
      seq += 1n;
    }
    stampPayload(payload, { phase: 2, runId, msgSize, seq });
    client.send(payload);
    const drainDeadlineNs = activeStopNs
      + BigInt(resolveSingleIdleDrainMs(options)) * 1_000_000n;
    while (currentEpochNs() < drainDeadlineNs) {
      await sleepImmediate();
    }
    stop = true;
    await recvTask;
    return collector.finish();
  } finally {
    client.close();
    server.close();
    ctx.close();
  }
}

module.exports = { runPairBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runPairBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'PAIR',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      options.libName,
      result.accepted
    )) {
      console.log(line);
    }
  })().catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
}
