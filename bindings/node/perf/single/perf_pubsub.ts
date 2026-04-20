// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');
const {
  applySocketPolicy,
  benchmarkEndpoint,
  drainRecvSocket,
  waitForPostReadySettle,
  waitForConnectionReady,
} = require('./perf_single_common');

async function runPubSubBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  const pub = new zlink.PubSocket(ctx);
  const sub = new zlink.SubSocket(ctx);
  const endpoint = await benchmarkEndpoint(options.transport, `pubsub-${msgSize}`);
  const topic = 'perf:pubsub';

  try {
    applySocketPolicy(pub, {
      noDrop: Number(process.env.PERF_SINGLE_PUBSUB_XPUB_NODROP ?? 1) !== 0
    });
    applySocketPolicy(sub, {
      recvTimeout: Number(
        process.env.PERF_SINGLE_PUBSUB_RCVTIMEO_MS
        ?? process.env.PERF_SINGLE_RCVTIMEO_MS
        ?? 200
      )
    });
    pub.bind(endpoint);
    sub.setSubscription(topic);
    await waitForConnectionReady(sub, () => sub.connect(endpoint));
    await waitForPostReadySettle(Number(process.env.PERF_SINGLE_PUBSUB_READY_SETTLE_MS ?? 1000));

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
      sub,
      (received) => {
        const header = decodeMetricHeader(received.parts[0].data());
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
        pub.publish(topic, payload);
        seq += 1n;
      }
      if (currentEpochNs() < activeStopNs) {
        await sleepImmediate();
      }
    }
    stampPayload(payload, { phase: 2, runId, msgSize, seq });
    pub.publish(topic, payload);
    const drainDeadlineNs = activeStopNs + 250_000_000n;
    while (currentEpochNs() < drainDeadlineNs) {
      await sleepImmediate();
    }
    stop = true;
    await recvTask;
    const result = await collector.finish();
    return result.latenciesNs;
  } finally {
    sub.close();
    pub.close();
    ctx.close();
  }
}

module.exports = { runPubSubBenchmark };
