// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../dist');
const {
  createMetricCollector,
  decodeMetricHeader,
  currentEpochUs,
  sleepImmediate,
  summarizeMetrics
} = require('../common/perf_metrics');
const { parseMultiArgs } = require('./perf_multi_common');

const TOPIC = 'perf.topic';
const CONTROL_TOPIC = 'perf.control';
const DEBUG_SPOT = Boolean(process.env.PERF_DEBUG_TRANSITIONS);

function debugSpot(message) {
  if (DEBUG_SPOT) {
    console.error(`[multi-spot-client] ${message}`);
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  if (!options.controlEndpoint) {
    throw new Error('missing --control-endpoint');
  }
  const ctx = new zlink.Context();
  const collector = createMetricCollector({
    runId: 0,
    msgSize: options.msgSize
  });
  const controlNode = new zlink.SpotNode(ctx);
  const controlSpot = new zlink.Spot(controlNode);
  const slots = [];
  let rl = null;
  let controlLocalEndpoint = '';
  let stop = false;
  let stopResolve;
  let timeoutId = null;
  let controlConnected = false;
  let controlConnectedResolve;
  const stopped = new Promise((resolve) => {
    stopResolve = resolve;
  });
  const controlConnectedPromise = new Promise((resolve) => {
    controlConnectedResolve = resolve;
  });
  const deadlineReached = new Promise((resolve) => {
    timeoutId = setTimeout(
      resolve,
      Math.ceil((options.warmup + options.duration + 2) * 1000)
    );
  });

  const handleDelivery = (received) => {
    const header = decodeMetricHeader(received.parts[0].data);
    if (!header) {
      return;
    }
    if (header.phase === 2) {
      stopResolve();
      return;
    }
    collector.record(header, currentEpochUs());
  };

  const publishControl = async (payload) => {
    const buffer = Buffer.from(payload);
    while (true) {
      const result = controlSpot.tryPublish(CONTROL_TOPIC, buffer);
      if (result === zlink.SendResult.Sent) {
        return;
      }
      await sleepImmediate();
    }
  };

  try {
    debugSpot('stdin watcher start');
    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    (async () => {
      for await (const line of rl) {
        if (line.startsWith('CONTROL_CONNECTED,')) {
          controlConnected = true;
          controlConnectedResolve();
          debugSpot(`stdin control connected ${line}`);
          continue;
        }
        if (line === 'STOP' || line === 'QUIT') {
          stop = true;
          stopResolve();
          break;
        }
      }
    })();

    controlSpot.setLinger(0);
    controlSpot.setSendHighWaterMark(Math.max(1024, options.clients * 8));
    controlSpot.setSendTimeout(200);
    controlSpot.setReceiveHighWaterMark(Math.max(1024, options.clients * 8));
    controlSpot.setReceiveTimeout(200);
    controlNode.bind('tcp://127.0.0.1:0');
    const controlStatus = controlNode.statusSnapshot();
    if (!controlStatus.localEndpoint) {
      throw new Error('failed to resolve client control endpoint');
    }
    controlLocalEndpoint = controlStatus.localEndpoint;
    debugSpot(`control endpoint ${controlStatus.localEndpoint}`);
    console.log(`CLIENT_CONTROL_ENDPOINT,${controlStatus.localEndpoint}`);
    controlNode.connectPeer(options.controlEndpoint);
    debugSpot(`control connect issued ${options.controlEndpoint}`);

    debugSpot(`create slots begin clients=${options.clients}`);
    for (let i = 0; i < options.clients; i += 1) {
      const node = new zlink.SpotNode(ctx);
      const spot = new zlink.Spot(node);
      spot.setLinger(0);
      spot.setReceiveHighWaterMark(100);
      spot.setReceiveTimeout(200);
      slots.push({ node, spot });
    }
    debugSpot(`create slots done count=${slots.length}`);

    debugSpot('connect data slots begin');
    for (const slot of slots) {
      slot.node.connectPeer(options.endpoint);
      slot.spot.setSubscription(TOPIC);
    }
    debugSpot('connect data slots done');

    debugSpot('before client ready');
    console.log(`CLIENT_READY,${options.msgSize}`);
    const recvTasks = slots.map((slot) => (async () => {
      while (!stop) {
        const received = slot.spot.trySubscribe();
        if (!received) {
          await new Promise((resolve) => setImmediate(resolve));
          continue;
        }
        handleDelivery(received);
      }
    })());

    debugSpot('await control connected');
    await controlConnectedPromise;
    debugSpot('control connected resolved');
    await publishControl('CONNECTED');
    debugSpot('published CONNECTED');
    await new Promise((resolve) => setTimeout(resolve, 25));
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
    const lines = summarizeMetrics(
      'MULTI_SPOT',
      'tcp',
      options.msgSize,
      result.latenciesUs,
      options.duration
    );
    for (const line of lines) {
      console.log(line);
    }
    process.exit(0);
  } finally {
    debugSpot('finally start');
    if (timeoutId) {
      clearTimeout(timeoutId);
      timeoutId = null;
    }
    try {
      controlNode.disconnectPeer(options.controlEndpoint);
    } catch (_) {
      // best-effort teardown
    }
    if (controlLocalEndpoint) {
      try {
        controlNode.disconnectPeer(controlLocalEndpoint);
      } catch (_) {
        // best-effort teardown
      }
    }
    await sleepImmediate();
    debugSpot('close control spot');
    controlSpot.close();
    debugSpot('close control node');
    controlNode.close();
    debugSpot('close readline');
    if (rl) {
      rl.close();
    }
    debugSpot('close collector');
    await collector.close();
    debugSpot('close slots');
    for (const slot of slots) {
      try {
        slot.node.disconnectPeer(options.endpoint);
      } catch (_) {
        // best-effort teardown
      }
      await sleepImmediate();
      slot.spot.close();
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
