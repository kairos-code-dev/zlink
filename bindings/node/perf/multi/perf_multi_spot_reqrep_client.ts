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
const { parseMultiArgs } = require('./perf_multi_common');
const { trySocketPublish, waitForConnectionReady } = require('./perf_multi_runtime');

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
  const controlPub = new zlink.PubSocket(ctx);
  const routers = [];
  const payloads = [];
  const waiting = [];

  try {
    if (!options.serverNodeRid || !options.serverSpotRid) {
      throw new Error('missing server routing ids');
    }
    const serverNodeRid = zlink.RoutingId.fromBytes(Buffer.from(options.serverNodeRid, 'hex'));
    const serverSpotRid = zlink.RoutingId.fromBytes(Buffer.from(options.serverSpotRid, 'hex'));
    controlPub.bind(options.controlEndpoint);
    for (let i = 0; i < options.clients; i += 1) {
      const router = new zlink.RouterSocket(ctx);
      routers.push(router);
      payloads.push(createPayload(options.msgSize));
      waiting.push(false);
    }
    for (const router of routers) {
      await waitForConnectionReady(router, () => router.connect(options.peerEndpoint));
    }

    while (!trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from('CONNECTED'))) {
      await sleepImmediate();
    }
    while (!trySocketPublish(controlPub, CONTROL_TOPIC, Buffer.from(`READY_COUNT,${options.msgSize},${routers.length}`))) {
      await sleepImmediate();
    }
    console.log(`CLIENT_READY,${options.msgSize}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line !== `START,${options.msgSize}`) {
        continue;
      }

      const runId = createRunId(1);
      const activeStartNs = process.hrtime.bigint();
      const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
      const collector = createMetricCollector({
        runId,
        msgSize: options.msgSize,
        activeStartNs,
        activeStopNs,
        roundTrip: true
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

      while (process.hrtime.bigint() < activeStopNs) {
        let progressed = false;
        for (let i = 0; i < routers.length; i += 1) {
          if (waiting[i]) {
            continue;
          }
          stampPayload(payloads[i], { phase: 1, runId, msgSize: options.msgSize, seq });
          const issued = routers[i].tryRequestToSpot(
            serverNodeRid,
            serverSpotRid,
            Buffer.from(payloads[i]),
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

      const drainDeadline = Date.now() + 2000;
      while (waiting.some(Boolean) && Date.now() < drainDeadline) {
        await sleepImmediate();
      }
      const result = await collector.finish();
      for (const metricLine of summarizeMetrics(
        'MULTI_SPOT_REQREP',
        'tcp',
        options.msgSize,
        result.latenciesNs,
        options.duration
      )) {
        console.log(metricLine);
      }
      break;
    }
  } finally {
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
