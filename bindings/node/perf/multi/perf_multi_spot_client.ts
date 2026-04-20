// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeader,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { trySocketPublish } = require('./perf_multi_runtime');

const TOPIC = 'perf.topic';
const CONTROL_TOPIC = 'perf.control';
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

function tryControlPublish(pub, payload) {
  return trySocketPublish(pub, CONTROL_TOPIC, Buffer.from(payload));
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  const controlPub = new zlink.PubSocket(ctx);
  const slots = [];
  let collector = null;

  try {
    controlPub.bind(options.controlEndpoint);
    console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
    for (let i = 0; i < options.clients; i += 1) {
      const node = new zlink.SpotNode(ctx);
      const dealer = new zlink.DealerSocket(ctx);
      node.attachChannelDealerManual(SERVICE_NAME, dealer);
      const spot = node.createSpot();
      spot.setSubscription(TOPIC);
      node.connectPeer(options.peerEndpoint);
      slots.push({ node, dealer, spot });
    }

    for (const slot of slots) {
      slot.spot.onDispatchEvent(() => {
        while (true) {
          const received = trySpotSubscribe(slot.spot);
          if (!received) {
            return;
          }
          if (!collector) {
            continue;
          }
          const firstPart = received.parts[0];
          if (!firstPart) {
            continue;
          }
          collector.record(
            decodeMetricHeader(firstPart.data()),
            currentEpochNs()
          );
        }
      });
    }

    const stabilizationDeadline = Date.now() + 1000;
    while (Date.now() < stabilizationDeadline) {
      await sleepImmediate();
    }
    while (!tryControlPublish(controlPub, 'CONNECTED')) {
      await sleepImmediate();
    }
    const controlSettleDeadline = Date.now() + 25;
    while (Date.now() < controlSettleDeadline) {
      await sleepImmediate();
    }
    while (!tryControlPublish(controlPub, `READY_COUNT,${options.msgSize},${slots.length}`)) {
      await sleepImmediate();
    }
    console.log(`CLIENT_READY,${options.msgSize}`);

    const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    for await (const line of rl) {
      if (line !== `START,${options.msgSize}`) {
        continue;
      }

      const activeStartNs = currentEpochNs();
      const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
      collector = createMetricCollector({
        runId: createRunId(1),
        msgSize: options.msgSize,
        activeStartNs,
        activeStopNs
      });
      while (currentEpochNs() < activeStopNs + 250_000_000n) {
        await sleepImmediate();
      }
      break;
    }

    const result = collector ? await collector.finish() : { latenciesNs: [] };
    for (const metricLine of summarizeMetrics(
      'MULTI_SPOT',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration
    )) {
      console.log(metricLine);
    }
  } finally {
    controlPub.close();
    for (const slot of slots) {
      slot.spot.close();
      slot.dealer.close();
      slot.node.close();
    }
    ctx.close();
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
