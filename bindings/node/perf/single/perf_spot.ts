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
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  resolveSingleIdleDrainMs,
  waitForPostReadySettle
} = require('./perf_single_common');

const TOPIC = 'perf.topic';
const SERVICE_TYPE_SPOT = 0x3002;
const SERVICE_NAME = 'perf.spot';

function trySpotSubscribe(spot) {
  try {
    return spot.subscribe(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

function drainSpot(spot, onMessage) {
  while (true) {
    const received = trySpotSubscribe(spot);
    if (!received) {
      return;
    }
    onMessage(received);
  }
}

async function waitForProbeReady(spot, payload, runId, msgSize, seqRef) {
  let ready = false;
  const deadline = Date.now() + Number(process.env.PERF_CONNECT_READY_TIMEOUT_MS ?? 5000);
  const waiters = [];

  spot.onDispatchEvent(() => {
      drainSpot(spot, (received) => {
        const header = decodeMetricHeaderFromParts(received.parts);
        if (!header) {
          return;
        }
        if (header.phase === 0 && header.runId === runId && header.msgSize === msgSize) {
          ready = true;
          while (waiters.length > 0) {
            waiters.shift()();
        }
      }
    });
  });

  while (!ready && Date.now() < deadline) {
    stampPayload(payload, {
      phase: 0,
      runId,
      msgSize,
      seq: seqRef.current
    });
    spot.publish(SERVICE_NAME, TOPIC, payload);
    seqRef.current += 1n;
    if (!ready) {
      await Promise.race([
        new Promise((resolve) => waiters.push(resolve)),
        sleepImmediate()
      ]);
    }
    await sleepImmediate();
  }

  if (!ready) {
    throw new Error('spot ready probe timed out');
  }
}

async function runSpotBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const node = new zlink.SpotNode(ctx);
  const discovery = new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, SERVICE_NAME);
  let spot = null;

  try {
    node.attachDiscovery(discovery);
    spot = node.createSpot();
    applySocketPolicy(spot, options);
    spot.setSubscription(TOPIC);

    const runId = createRunId(options.runId ?? 1);
    const payload = createPayload(msgSize);
    const seqRef = { current: 1n };
    await waitForProbeReady(spot, payload, runId, msgSize, seqRef);
    await waitForPostReadySettle(Number(process.env.PERF_SINGLE_SPOT_READY_SETTLE_MS ?? 1000));

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs,
      sampleCap: resolveSingleLatencySampleCap()
    });

    spot.onDispatchEvent(() => {
      drainSpot(spot, (received) => {
        collector.record(
          decodeMetricHeaderFromParts(received.parts),
          currentEpochNs()
        );
      });
    });

    while (currentEpochNs() < activeStopNs) {
      stampPayload(payload, {
        phase: 1,
        runId,
        msgSize,
        seq: seqRef.current
      });
      spot.publish(SERVICE_NAME, TOPIC, payload);
      seqRef.current += 1n;
    }
    stampPayload(payload, {
      phase: 2,
      runId,
      msgSize,
      seq: seqRef.current
    });
    spot.publish(SERVICE_NAME, TOPIC, payload);
    await waitForPostReadySettle(resolveSingleIdleDrainMs(options));
    return collector.finish();
  } finally {
    if (spot) {
      spot.close();
    }
    discovery.close();
    ctx.close();
  }
}

module.exports = { runSpotBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runSpotBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'SPOT',
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
