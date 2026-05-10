// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../..');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  summarizeMetrics,
} = require('../common/perf_metrics');
const {
  applyContextPolicy,
  applySocketPolicy,
  benchmarkEndpoint,
  closeSenderWorker,
  configureTlsServer,
  drainRecvSocket,
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  spawnSenderWorker,
  waitForWorkerDone,
  waitForWorkerError,
  waitForMonitorConnectionReady,
  waitForWorkerMessage,
} = require('./perf_single_common');

async function runPairBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const server = new zlink.PairSocket(ctx);
  const serverMonitor = server.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
  const endpoint = await benchmarkEndpoint(options.transport, `pair-${msgSize}`);
  let worker = null;

  try {
    applySocketPolicy(server, options);
    configureTlsServer(server, options.transport);
    server.bind(endpoint);
    worker = spawnSenderWorker({
      kind: 'pair',
      transport: options.transport,
      endpoint,
      duration: options.duration,
      msgSize,
      runId: options.runId ?? 1,
      options,
    });
    const workerError = waitForWorkerError(worker);
    await Promise.race([
      waitForWorkerMessage(worker, 'ready'),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    await waitForMonitorConnectionReady(serverMonitor);

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

    // PERF_SINGLE_TEST_POLICY § 1.4: receiver drains until the wire stop
    // token arrives. The deadline-based idle drain is no longer needed —
    // in-flight messages naturally precede the stop token on the wire.
    const recvTask = drainRecvSocket(
      server,
      (received) => {
        const header = decodeMetricHeaderFromParts(received.parts);
        collector.record(header, currentEpochNs());
      }
    );

    worker.postMessage({ type: 'start' });
    await Promise.race([
      waitForWorkerDone(worker, options.duration),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    await recvTask;
    return collector.finish();
  } finally {
    await closeSenderWorker(worker);
    serverMonitor.close();
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
