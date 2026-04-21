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
  configureTlsClient,
  configureTlsServer,
  drainRecvSocket,
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  resolveSingleIdleDrainMs,
  spawnSenderWorker,
  waitForMonitorConnectionReady,
  waitForWorkerMessage,
} = require('./perf_single_common');

async function runDealerDealerBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const server = new zlink.DealerSocket(ctx);
  const serverMonitor = server.monitorOpen(zlink.MonitorEvent.CONNECTION_READY);
  const endpoint = await benchmarkEndpoint(options.transport, `dealer-dealer-${msgSize}`);
  let worker = null;

  try {
    applySocketPolicy(server, options);
    configureTlsServer(server, options.transport);
    server.bind(endpoint);
    worker = spawnSenderWorker({
      kind: 'dealer_dealer',
      transport: options.transport,
      endpoint,
      duration: options.duration,
      msgSize,
      runId: options.runId ?? 1,
      options,
    });
    const workerError = waitForWorkerMessage(worker, 'error');
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
    let stop = false;

    const recvTask = drainRecvSocket(
      server,
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
      + BigInt(resolveSingleIdleDrainMs(options)) * 1_000_000n;
    while (currentEpochNs() < drainDeadlineNs) {
      await sleepImmediate();
    }
    stop = true;
    await recvTask;
    return collector.finish();
  } finally {
    await closeSenderWorker(worker);
    serverMonitor.close();
    server.close();
    ctx.close();
  }
}

module.exports = { runDealerDealerBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runDealerDealerBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'DEALER_DEALER',
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
