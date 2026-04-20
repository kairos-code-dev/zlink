// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');
const {
  parseMultiArgs,
  resolveMultiSpotControlSettleMs,
  resolveMultiSpotReadySettleMs
} = require('./perf_multi_common');
const {
  applySocketPolicy,
  applyContextPolicy,
  resolveMultiLatencySampleCap,
  subscribeNoWait,
  trySocketPublish,
  waitForConnectionReady
} = require('./perf_multi_runtime');

const CONTROL_TOPIC = 'perf.control';

function closeMessageParts(parts) {
  for (const part of parts || []) {
    if (part && typeof part.close === 'function') {
      part.close();
    }
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_SPOT_REQREP');
  const controlPub = new zlink.PubSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  const routers = [];
  const payloads = [];
  const waiting = [];

  try {
    if (!options.serverNodeRid || !options.serverSpotRid) {
      throw new Error('missing server routing ids');
    }
    const serverNodeRid = zlink.RoutingId.fromBytes(Buffer.from(options.serverNodeRid, 'hex'));
    const serverSpotRid = zlink.RoutingId.fromBytes(Buffer.from(options.serverSpotRid, 'hex'));
    applySocketPolicy(controlPub);
    applySocketPolicy(controlSub);
    controlPub.bind(options.controlEndpoint);
    console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
    controlSub.setSubscription(CONTROL_TOPIC);
    await waitForConnectionReady(controlSub, () => controlSub.connect(options.serverControlEndpoint));
    console.log(`CONTROL_CONNECTED,${options.serverControlEndpoint}`);
    for (let i = 0; i < options.clients; i += 1) {
      const router = new zlink.RouterSocket(ctx);
      applySocketPolicy(router);
      routers.push(router);
      payloads.push(createPayload(options.msgSize));
      waiting.push(false);
    }
    for (const router of routers) {
      await waitForConnectionReady(router, () => router.connect(options.peerEndpoint));
    }

    const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
    while (Date.now() < stabilizationDeadline) {
      await sleepImmediate();
    }
    while (!trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from('CONNECTED'))) {
      await sleepImmediate();
    }
    const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
    while (Date.now() < controlSettleDeadline) {
      await sleepImmediate();
    }
    while (!trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`READY_COUNT,${options.msgSize},${routers.length}`))) {
      await sleepImmediate();
    }
    console.log(`CLIENT_READY,${options.msgSize}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    let startRequested = false;
    let startBroadcast = false;
    (async () => {
      for await (const line of rl) {
        if (line === `START,${options.msgSize}`) {
          startRequested = true;
        }
      }
    })();

    while (!(startRequested && startBroadcast)) {
      while (true) {
        const received = subscribeNoWait(controlSub);
        if (!received) {
          break;
        }
        const payloadText = received.parts[0].data().toString('utf8');
        if (payloadText === `START,${options.msgSize}`) {
          startBroadcast = true;
        }
      }
      await sleepImmediate();
    }

    const runId = createRunId(1);
    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    const collector = createMetricCollector({
      runId,
      msgSize: options.msgSize,
      activeStartNs,
      activeStopNs,
      roundTrip: true,
      sampleCap: resolveMultiLatencySampleCap()
    });
    let seq = 1n;

    const onReply = (index, result, replyParts) => {
      try {
        if (result === zlink.RequestResult.Ok && replyParts.length > 0) {
          collector.record(
            decodeMetricHeader(replyParts[0].data()),
            currentEpochNs()
          );
        }
      } finally {
        closeMessageParts(replyParts);
        waiting[index] = false;
      }
    };

    while (currentEpochNs() < activeStopNs) {
      let progressed = false;
      for (let i = 0; i < routers.length; i += 1) {
        if (waiting[i]) {
          continue;
        }
        stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
        const issued = routers[i].tryRequestToSpot(
          serverNodeRid,
          serverSpotRid,
          payloads[i],
          (result, replyParts) => onReply(i, result, replyParts),
          2000
        );
        if (!issued) {
          continue;
        }
        waiting[i] = true;
        seq += 1n;
        progressed = true;
      }
      if (!progressed) {
        await sleepImmediate();
      }
    }

    const result = await collector.finish();
    for (const metricLine of summarizeMetrics(
      'MULTI_SPOT_REQREP',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      'current',
      result.accepted
    )) {
      console.log(metricLine);
    }
  } finally {
    controlSub.close();
    controlPub.close();
    for (const router of routers) {
      router.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
