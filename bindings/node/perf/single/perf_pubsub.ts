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
  applyContextPolicy,
  applySocketPolicy,
  benchmarkEndpoint,
  closeSenderWorker,
  drainRecvSocket,
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  resolveSingleIdleDrainMs,
  spawnSenderWorker,
  waitForPostReadySettle,
  waitForConnectionReady,
  waitForWorkerMessage,
} = require('./perf_single_common');

async function runPubSubBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);
  const endpoint = await benchmarkEndpoint(options.transport, `pubsub-${msgSize}`);
  const topic = 'perf:pubsub';
  let worker = null;

  try {
    applySocketPolicy(sub, {
      ...options,
      recvTimeout: Number(
        process.env.PERF_SINGLE_PUBSUB_RCVTIMEO_MS
        ?? process.env.PERF_SINGLE_RCVTIMEO_MS
        ?? 200
      )
    });
    worker = spawnSenderWorker({
      kind: 'pubsub',
      transport: options.transport,
      endpoint,
      duration: options.duration,
      msgSize,
      runId: options.runId ?? 1,
      options: {
        ...options,
        noDrop: Number(process.env.PERF_SINGLE_PUBSUB_XPUB_NODROP ?? 1) !== 0
      },
    });
    const workerError = waitForWorkerMessage(worker, 'error');
    await Promise.race([
      waitForWorkerMessage(worker, 'bound'),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    sub.setSubscription(topic);
    await waitForConnectionReady(sub, () => sub.connect(endpoint));
    await waitForPostReadySettle(Number(process.env.PERF_SINGLE_PUBSUB_READY_SETTLE_MS ?? 1000));
    worker.postMessage({ type: 'ready' });

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    const runId = createRunId(options.runId ?? 1);
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs,
      sampleCap: resolveSingleLatencySampleCap()
    });
    const payload = createPayload(msgSize);
    let seq = 1n;
    let stop = false;

    const recvTask = drainRecvSocket(
      sub,
      (received) => {
        const header = decodeMetricHeaderFromParts(received.parts);
        collector.record(header, currentEpochNs());
      },
      () => stop
    );

    worker.postMessage({ type: 'start' });
    await Promise.race([
      waitForWorkerMessage(worker, 'done'),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    const drainDeadlineNs = activeStopNs
      + BigInt(resolveSingleIdleDrainMs({
        ...options,
        recvTimeoutMs: Number(
          process.env.PERF_SINGLE_PUBSUB_RCVTIMEO_MS
          ?? process.env.PERF_SINGLE_RCVTIMEO_MS
          ?? 200
        )
      })) * 1_000_000n;
    while (currentEpochNs() < drainDeadlineNs) {
      await sleepImmediate();
    }
    stop = true;
    await recvTask;
    return collector.finish();
  } finally {
    await closeSenderWorker(worker);
    sub.close();
    ctx.close();
  }
}

module.exports = { runPubSubBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runPubSubBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'PUBSUB',
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
