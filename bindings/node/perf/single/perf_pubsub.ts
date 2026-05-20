// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('@zlink-systems/zlink');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  HEADER_SIZE,
  summarizeMetrics,
} = require('../common/perf_metrics');
const {
  applyContextPolicy,
  applyAutoHwmMsgUnit,
  applySocketPolicy,
  benchmarkEndpoint,
  closeSenderWorker,
  configureTlsClient,
  drainRecvSocket,
  emitSingleSocketHwmDetail,
  parseSingleBinaryArgs,
  runLocalSocketOneWayBenchmark,
  spawnSenderWorker,
  waitForWorkerError,
  waitForPostReadySettle,
  waitForMonitorConnectionReady,
  waitForWorkerMessage,
} = require('./perf_single_common');
const { STOP_TOKEN_BYTES } = require('../perf_stop_token');

function trace(message) {
  if (process.env.PERF_NODE_TRACE === '1') {
    console.error(`[pubsub] ${message}`);
  }
}

async function runPubSubBenchmark(msgSize, options) {
  if (options.transport === 'inproc') {
    // inproc is context-local (doc/guide/04-transports.md) so the Worker
    // sender path cannot reach it. Run PUB+SUB in one shared context with
    // the C-faithful blocking-equivalent one-way model (same adaptation
    // as PAIR/DEALER inproc). C perf_pubsub.cpp: PUB binds, SUB connects
    // + subscribes; wire stop token on the topic ends the phase.
    const TOPIC = 'bench';
    return runLocalSocketOneWayBenchmark({
      pattern: 'PUBSUB',
      msgSize,
      options,
      endpointToken: 'pubsub',
      createReceiver: (ctx) => new zlink.SubSocket(ctx),
      createSender: (ctx) => new zlink.PubSocket(ctx),
      configureReceiver: (socket) => socket.setSubscription(TOPIC),
      senderBinds: true,
      drainViaSubscribe: true,
      // C perf_pubsub.cpp emits AUTO_HWM_DETAIL for the PUB as
      // `publisher` and the SUB as `subscriber`; mirror those labels so
      // the `## Auto-HWM Detail` PUBSUB block is byte-identical to C
      // (receiver=SUB=subscriber, sender=PUB=publisher).
      receiverHwmComponent: 'subscriber',
      senderHwmComponent: 'publisher',
      sendActive: (socket, payload) => {
        try {
          return socket.publish(TOPIC).message(payload)
            .flags(zlink.SendFlags.DontWait).submit();
        } catch (error) {
          if (error instanceof zlink.SubmitError
            && (error.result === zlink.SubmitResult.Backpressured
              || error.result === zlink.SubmitResult.NotConnected
              || error.result === zlink.SubmitResult.NotFound)) {
            return false;
          }
          const text = String(error && error.message ? error.message : error);
          if ((error && error.code === 'EAGAIN')
            || text.includes('Resource temporarily unavailable')) {
            return false;
          }
          throw error;
        }
      },
      sendStop: (socket) => {
        socket.publish(TOPIC).message(STOP_TOKEN_BYTES)
          .flags(zlink.SendFlags.None).submit();
      },
    });
  }

  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const sub = new zlink.SubSocket(ctx);
  const subMonitor = sub.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
  const endpoint = await benchmarkEndpoint(options.transport, `pubsub-${msgSize}`);
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
    applyAutoHwmMsgUnit(ctx, msgSize);
    ctx.recalculateAutoHwm();
    // The SUB connects from this (main) process, so it must carry the TLS
    // client config for tls/wss exactly like the worker's connecting
    // sockets do (perf_single_sender_worker.ts:51). Without it the SUB
    // cannot complete the TLS handshake with the TLS PUB bind and the
    // CONNECTION_READY gate never fires (PUBSUB tls/wss hung pre-data).
    configureTlsClient(sub, options.transport);
    sub.setSubscription('');
    sub.connect(endpoint);
    worker = spawnSenderWorker({
      kind: 'pubsub',
      transport: options.transport,
      endpoint,
      duration: options.duration,
      msgSize,
      runId: options.runId ?? 1,
      topic: 'bench',
      options: {
        ...options,
        noDrop: process.env.PERF_SINGLE_PUBSUB_XPUB_NODROP === '0'
          ? false
          : true
      },
    });
    const workerError = waitForWorkerError(worker);
    await Promise.race([
      waitForWorkerMessage(worker, 'bound'),
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    // C setup_connected_pubsub_pair: wait for the subscriber
    // CONNECTION_READY and the post-ready settle BEFORE releasing the
    // publisher's active loop, so the active window starts on a settled
    // connected pair (not an extra start gate — this is the ready gate).
    await waitForMonitorConnectionReady(subMonitor);
    trace('connection ready');
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
    });

    // PERF_SINGLE_TEST_POLICY § 1.4 / § 2.0.1 / C setup_connected_pubsub_
    // pair + send_pubsub_stop_token: the `bound`->CONNECTION_READY->settle
    // ->`ready` exchange above IS the connection-ready gate (PUB binds in
    // the worker, SUB connects here). No extra start/stop ack — the
    // subscriber drains until the wire stop token on the topic.
    const recvTask = drainRecvSocket(
      sub,
      (received) => {
        const header = decodeMetricHeaderFromParts(received.parts, Math.max(msgSize, HEADER_SIZE));
        collector.record(header, currentEpochNs());
      },
      { recordUntilNs: activeStopNs }
    );
    await Promise.race([
      recvTask,
      workerError.then((message) => Promise.reject(new Error(message.message)))
    ]);
    const result = collector.finish();
    emitSingleSocketHwmDetail(sub, 'PUBSUB', options.transport, 'subscriber', msgSize);
    return result;
  } finally {
    trace('closing');
    await closeSenderWorker(worker);
    subMonitor.close();
    sub.close();
    trace('sub closed');
    ctx.close();
    trace('ctx closed');
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
