// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  HEADER_SIZE,
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
  subscribeNoWaitInto,
  waitForConnectionReady
} = require('./perf_multi_runtime');
const { isStopTokenParts } = require('../perf_stop_token');

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_PUBSUB');
  const subs = [];
  const receivedBuffers = [];
  let rl = null;
  let collector = null;

  try {
    for (let i = 0; i < options.clients; i += 1) {
      const sub = new zlink.SubSocket(ctx);
      applySocketPolicy(sub);
      configureTlsClient(sub, options.transport);
      sub.setSubscription('perf.topic');
      await waitForConnectionReady(sub, () => sub.connect(options.endpoint));
      applyAutoHwmMsgUnit(ctx, options.msgSize);
      subs.push(sub);
      receivedBuffers.push(new zlink.TopicMessage());
    }
    ctx.recalculateAutoHwm();
    for (const sub of subs) {
      emitMultiSocketHwmDetail(sub, 'endpoint', options.transport, options.msgSize);
    }

    console.log(`CLIENT_READY,${options.msgSize}`);
    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line === `START,${options.msgSize}`) {
        const activeStartNs = currentEpochNs();
        const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
        const payloadSize = Math.max(options.msgSize, HEADER_SIZE);
        collector = createMetricCollector({
          runId: createRunId(1),
          msgSize: options.msgSize,
          activeStartNs,
          activeStopNs,
        });
        const poller = new zlink.Poller();
        try {
          for (let i = 0; i < subs.length; i += 1) {
            poller.add(subs[i], pollEvents(POLLIN), i);
          }
          // C parity: bindings/c/perf/multi/src/perf_multi_pubsub_client
          // .cpp run_recv_duration (~166-228). Pure signal-driven `-1`
          // poller wait — NO zlink.Timer, NO duration+2s wall-clock bound.
          // The active window closes on the application clock (the
          // collector enforces the recvTs<=activeStop anchor —
          // perf_measurement.ts ~280) and the phase ends when the
          // server's wire stop token wakes the `-1` wait. The stop token
          // is checked BEFORE decoding the metric header (C lines 76-79 /
          // 203-206; mirrors the already-fixed cpp perf_pubsub_client.cpp
          // run_active_until_stop_token ~239-246).
          let stopReceived = false;
          while (!stopReceived) {
            const ready = poller.waitMany(poller.size, -1);
            if (!ready || ready.length === 0) {
              continue;
            }
            for (const event of ready) {
              const index = event.tag ?? event.userData;
              if (!Number.isInteger(index)) {
                continue;
              }
              const received = receivedBuffers[index];
              while (true) {
                if (!subscribeNoWaitInto(subs[index], received)) {
                  break;
                }
                if (isStopTokenParts(received.parts)) {
                  stopReceived = true;
                  continue;
                }
                collector.record(
                  decodeMetricHeaderFromParts(received.parts, payloadSize),
                  currentEpochNs()
                );
              }
            }
          }
        } finally {
          poller.close();
        }
        break;
      }
    }

    const result = collector ? await collector.finish() : { latenciesNs: [] };
    for (const line of summarizeMetrics(
      'MULTI_PUBSUB',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      'current',
      result.accepted
    )) {
      console.log(line);
    }
    console.log(`CLIENT_DONE,${options.msgSize}`);
  } finally {
    rl?.close();
    for (const received of receivedBuffers) {
      received.close();
    }
    for (const sub of subs) {
      sub.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
