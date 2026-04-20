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
  const deadline = Date.now() + 10_000;
  const waiters = [];

  spot.onDispatchEvent(() => {
    drainSpot(spot, (received) => {
      const firstPart = received.parts[0];
      if (!firstPart) {
        return;
      }
      const header = decodeMetricHeader(firstPart.data());
      if (!header) {
        return;
      }
      if (header.runId === runId && header.msgSize === msgSize) {
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
        new Promise((resolve) => setTimeout(resolve, 25))
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
  const node = new zlink.SpotNode(ctx);
  const discovery = new zlink.Discovery(ctx, SERVICE_TYPE_SPOT, SERVICE_NAME);
  let spot = null;

  try {
    node.attachDiscovery(discovery);
    spot = node.createSpot();
    spot.setSubscription(TOPIC);

    const runId = createRunId(options.runId ?? 1);
    const payload = createPayload(msgSize);
    const seqRef = { current: 1n };
    await waitForProbeReady(spot, payload, runId, msgSize, seqRef);
    await waitForPostReadySettle(Number(process.env.PERF_SINGLE_SPOT_READY_SETTLE_MS ?? 1000));

    const activeStartNs = process.hrtime.bigint();
    const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs
    });

    spot.onDispatchEvent(() => {
      drainSpot(spot, (received) => {
        const firstPart = received.parts[0];
        if (!firstPart) {
          return;
        }
        collector.record(
          decodeMetricHeader(firstPart.data()),
          currentEpochNs()
        );
      });
    });

    while (process.hrtime.bigint() < activeStopNs) {
      for (let i = 0; i < 256 && process.hrtime.bigint() < activeStopNs; i += 1) {
        stampPayload(payload, {
          phase: 1,
          runId,
          msgSize,
          seq: seqRef.current
        });
        spot.publish(SERVICE_NAME, TOPIC, payload);
        seqRef.current += 1n;
      }
      await sleepImmediate();
    }

    await waitForPostReadySettle(250);
    const result = await collector.finish();
    return result.latenciesNs;
  } finally {
    if (spot) {
      spot.close();
    }
    discovery.close();
    ctx.close();
  }
}

module.exports = { runSpotBenchmark };
