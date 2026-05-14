// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('@zlink-systems/zlink');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  summarizeMetrics,
} = require('../common/perf_metrics');
const {
  applyContextPolicy,
  applyAutoHwmMsgUnit,
  applySocketPolicy,
  benchmarkEndpoint,
  closeSenderWorker,
  configureTlsServer,
  drainRecvSocket,
  emitSingleSocketHwmDetail,
  parseSingleBinaryArgs,
  spawnSenderWorker,
  waitForWorkerDone,
  waitForWorkerError,
  waitForMonitorConnectionReady,
  waitForWorkerMessage,
} = require('./perf_single_common');

const RECEIVER_ID = Buffer.from('router-perf-receiver', 'ascii');
const SENDER_ID = Buffer.from('router-perf-sender', 'ascii');
const RECEIVER_ROUTING_ID = zlink.RoutingId.fromBytes(RECEIVER_ID);

function trace(message) {
  if (process.env.PERF_NODE_TRACE === '1') {
    console.error(`[router-router] ${message}`);
  }
}

function partStrings(received) {
  return received.parts.map((part) => part.data().toString());
}

function handshakeRouterReceiver(receiver) {
  const ping = new zlink.Received();
  receiver.recv(ping);
  try {
    if (ping.routingId === null || partStrings(ping).join(',') !== 'PING') {
      throw new Error('router-router handshake receive failed');
    }

    receiver.send(ping.routingId).message(Buffer.from('PONG')).submit();
    return ping.routingId;
  } finally {
    ping.close();
  }
}

async function runRouterRouterBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const receiver = new zlink.RouterSocket(ctx);
  const receiverMonitor = receiver.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
  const endpoint = await benchmarkEndpoint(options.transport, `router-router-${msgSize}`);
  let worker = null;

  try {
    applySocketPolicy(receiver, options);
    applyAutoHwmMsgUnit(receiver, msgSize);
    ctx.recalculateAutoHwm();
    receiver.setRoutingId(RECEIVER_ROUTING_ID);
    configureTlsServer(receiver, options.transport);
    receiver.bind(endpoint);
    worker = spawnSenderWorker({
      kind: 'router_router',
      transport: options.transport,
      endpoint,
      duration: options.duration,
      msgSize,
      runId: options.runId ?? 1,
      receiverRoutingIdBytes: RECEIVER_ID,
      senderRoutingIdBytes: SENDER_ID,
      options,
    });
    const workerError = waitForWorkerError(worker);
    await Promise.race([
      waitForWorkerMessage(worker, 'connected'),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    await waitForMonitorConnectionReady(receiverMonitor);
    worker.postMessage({ type: 'handshake' });
    handshakeRouterReceiver(receiver);
    await Promise.race([
      waitForWorkerMessage(worker, 'ready'),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    trace('handshake done');

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    const runId = createRunId(options.runId ?? 1);
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs,
    });

    worker.postMessage({ type: 'start' });
    await Promise.race([
      waitForWorkerMessage(worker, 'started'),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    const recvTask = drainRecvSocket(
      receiver,
      (received) => {
        const header = decodeMetricHeaderFromParts(received.parts);
        collector.record(header, currentEpochNs());
      }
    );
    await Promise.race([
      waitForWorkerDone(worker, options.duration),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    await recvTask;
    const result = collector.finish();
    emitSingleSocketHwmDetail(receiver, 'ROUTER_ROUTER', options.transport, 'receiver', msgSize);
    return result;
  } finally {
    trace('closing');
    await closeSenderWorker(worker);
    receiverMonitor.close();
    receiver.close();
    trace('receiver closed');
    ctx.close();
    trace('ctx closed');
  }
}

module.exports = { runRouterRouterBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runRouterRouterBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'ROUTER_ROUTER',
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
