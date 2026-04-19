// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../dist/canonical');
const {
  createMetricCollector,
  decodeMetricHeader,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');
const { waitForConnectionReady } = require('./perf_multi_runtime');

const TOPIC = 'perf.topic';
const CONTROL_TOPIC = 'perf.control';
const SERVICE_TYPE_SPOT = 0x3002;
const SERVICE_NAME = 'perf.spot';
const DEBUG_SPOT = Boolean(process.env.PERF_DEBUG_TRANSITIONS);
const READY_SETTLE_MS = Number(process.env.PERF_SPOT_READY_SETTLE_MS ?? 1000);
const tryRawPublish = (socket, topic, payload) => {
  try {
    socket.publish(topic, payload, zlink.SendFlags.DontWait);
    return true;
  } catch (error) {
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    throw error;
  }
};
const trySpotSubscribe = (spot) => {
  try {
    return spot.subscribe(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
};
const trySpotPublish = (spot, serviceName, topic, payload) => {
  try {
    spot.publish(serviceName, topic, payload, zlink.SendFlags.DontWait);
    return true;
  } catch (error) {
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    throw error;
  }
};

function debugSpot(message) {
  if (DEBUG_SPOT) {
    console.error(`[multi-spot-client] ${message}`);
  }
}

function createAttachedSpotNode(ctx) {
  const node = new zlink.SpotNode(ctx);
  const dealer = new zlink.DealerSocket(ctx);
  return { node, dealer };
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  if (!options.controlEndpoint) {
    throw new Error('missing --control-endpoint');
  }
  if (!options.peerEndpoint) {
    throw new Error('missing --peer-endpoint');
  }
  const ctx = new zlink.Context();
  const collector = createMetricCollector({
    runId: 0,
    msgSize: options.msgSize
  });
  const controlPub = new zlink.PubSocket(ctx);
  const slots = [];
  let stop = false;
  let stopResolve;
  let timeoutId = null;
  const stopped = new Promise((resolve) => {
    stopResolve = resolve;
  });
  const deadlineReached = new Promise((resolve) => {
    timeoutId = setTimeout(
      resolve,
      Math.ceil((options.warmup + options.duration + 2) * 1000)
    );
  });

  const handleDelivery = (received) => {
    const firstPart = received.parts[0];
    if (!firstPart) {
      return;
    }
    const header = decodeMetricHeader(firstPart.data());
    if (!header) {
      return;
    }
    if (header.phase === 2) {
      stopResolve();
      return;
    }
    collector.record(header, currentEpochNs());
  };

  const publishControl = async (payload) => {
    const buffer = Buffer.from(payload);
    while (true) {
      const result = tryRawPublish(controlPub, CONTROL_TOPIC, buffer);
      if (result) {
        return;
      }
      await sleepImmediate();
    }
  };

  try {
    controlPub.bind(options.controlEndpoint);
    await waitForConnectionReady(controlPub);

    debugSpot(`create slots begin clients=${options.clients}`);
    for (let i = 0; i < options.clients; i += 1) {
      const {
        node,
        dealer
      } = createAttachedSpotNode(ctx);
      slots.push({ node, dealer, spot: null });
    }
    debugSpot(`create slots done count=${slots.length}`);

    debugSpot('connect data slots begin');
    for (const slot of slots) {
      slot.node.attachChannelDealerManual(SERVICE_NAME, slot.dealer);
      slot.spot = slot.node.createSpot();
      slot.node.connectPeer(options.peerEndpoint);
      slot.spot.setSubscription(TOPIC);
    }
    debugSpot('connect data slots done');

    debugSpot('before client ready');
    console.log(`CLIENT_READY,${options.msgSize}`);
    const recvTasks = slots.map((slot) => (async () => {
      while (!stop) {
        const received = trySpotSubscribe(slot.spot);
        if (!received) {
          await new Promise((resolve) => setImmediate(resolve));
          continue;
        }
        handleDelivery(received);
      }
    })());

    await sleepImmediate();
    await publishControl('CONNECTED');
    debugSpot('published CONNECTED');
    await new Promise((resolve) => setTimeout(resolve, READY_SETTLE_MS));
    await publishControl(`READY_COUNT,${options.msgSize},${slots.length}`);
    debugSpot('published READY_COUNT');

    await Promise.race([
      stopped,
      deadlineReached
    ]);
    debugSpot('post race');
    stop = true;
    if (timeoutId) {
      clearTimeout(timeoutId);
      timeoutId = null;
    }
    debugSpot('before recvTasks join');
    await Promise.all(recvTasks);
    debugSpot('recvTasks joined');

    const result = await collector.finish();
    debugSpot('collector finished');
    await publishControl(`DONE_COUNT,${options.msgSize},${slots.length}`);
    debugSpot('published DONE_COUNT');
    const lines = summarizeMetrics(
      'MULTI_SPOT',
      'tcp',
      options.msgSize,
      result.latenciesNs,
      options.duration
    );
    for (const line of lines) {
      console.log(line);
    }
  } finally {
    debugSpot('finally start');
    if (timeoutId) {
      clearTimeout(timeoutId);
      timeoutId = null;
    }
    debugSpot('close control pub');
    controlPub.close();
    debugSpot('close collector');
    await collector.close();
    debugSpot('close slots');
    for (const slot of slots) {
      await sleepImmediate();
      if (slot.spot) {
        slot.spot.close();
      }
      slot.dealer.close();
      slot.node.close();
    }
    debugSpot('close ctx');
    ctx.close();
    debugSpot('finally done');
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
