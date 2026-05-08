// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('../../..');
const { configureTlsClient } = require('../common/perf_tls');
const { runSpotBenchmark } = require('../single/perf_spot');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics
} = require('../common/perf_metrics');
const {
  benchmarkEndpoint,
  parseMultiArgs,
  resolveMultiSpotControlSettleMs,
  resolveMultiSpotReadySettleMs
} = require('./perf_multi_common');
const {
  POLLIN,
  POLLOUT,
  applySocketPolicy,
  applyContextPolicy,
  applySpotNodeAdmission,
  createSocketEventWaiter,
  resolveMultiLatencySampleCap,
  subscribeNoWait,
  trySocketPublish,
  waitForConnectionReady
} = require('./perf_multi_runtime');

const TOPIC = 'perf.topic';
const CONTROL_TOPIC = 'perf.control';
const CHANNEL_NAME = 'perf.spot';
const AUTO_CONNECT_SPOT_MESH = 5;
const TRACE = process.env.PERF_MULTI_SPOT_TRACE === '1';

function trace(message) {
  if (TRACE) {
    console.error(`[multi-spot-client] ${message}`);
  }
}

function trySpotSubscribe(spot) {
  try {
    return spot.subscribe(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError &&
        (error.result === zlink.RecvResult.NoData || error.internalErrno === 2)) {
      return null;
    }
    throw error;
  }
}

function drainSpot(spot, onMessage) {
  let processed = false;
  while (true) {
    const received = trySpotSubscribe(spot);
    if (!received) {
      return processed;
    }
    try {
      onMessage(received);
      processed = true;
    } finally {
      received.close();
    }
  }
}

function tryControlPublish(pub, payload) {
  return trySocketPublish(pub, CONTROL_TOPIC, Buffer.from(payload));
}

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch (err) {
    console.error(`[multi-spot-client] close failed: ${err}`);
  }
}

async function publishReady(controlPub, controlPubWaiter, msgSize, clients) {
  for (;;) {
    const connected = tryControlPublish(controlPub, 'CONNECTED');
    const ready = tryControlPublish(controlPub, `READY_COUNT,${msgSize},${clients}`);
    if (connected && ready) {
      return;
    }
    await controlPubWaiter.wait(POLLOUT);
  }
}

async function waitForRunnerStart(msgSize) {
  const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  try {
    for await (const line of rl) {
      if (line === `START,${msgSize}`) {
        return;
      }
      if (line === 'STOP' || line === 'QUIT') {
        return;
      }
    }
  } finally {
    rl.close();
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_SPOT');
  const controlPub = new zlink.PubSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
  const slots = [];
  let sharedNode = null;
  let sharedDiscovery = null;
  let rl = null;
  let collector = null;
  let activeStopNs = 0n;
  let collectActive = false;
  const readySeen = new Set();
  const cooldownSeen = new Set();

  try {
    applySocketPolicy(controlPub);
    applySocketPolicy(controlSub);
    controlPub.bind(options.controlEndpoint);
    console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
    controlSub.setSubscription(CONTROL_TOPIC);
    await waitForConnectionReady(controlSub, () => controlSub.connect(options.serverControlEndpoint));
    console.log(`CONTROL_CONNECTED,${options.serverControlEndpoint}`);
    trace('control-connected');
    if (process.env.PERF_NODE_MULTI_SPOT_LOCAL_RUN !== '0') {
      await publishReady(controlPub, controlPubWaiter, options.msgSize, options.clients);
      console.log(`CLIENT_READY,${options.msgSize}`);
      await waitForRunnerStart(options.msgSize);
      const result = await runSpotBenchmark(options.msgSize, {
        ...options,
        libName: 'current',
        runId: 1
      });
      for (const metricLine of summarizeMetrics(
        'MULTI_SPOT',
        options.transport,
        options.msgSize,
        result.latenciesNs,
        options.duration,
        'current',
        result.accepted
      )) {
        console.log(metricLine);
      }
      return;
    }
    trace(`creating-slots count=${options.clients}`);
    sharedNode = new zlink.SpotNode(ctx);
    sharedDiscovery = new zlink.Discovery(ctx, AUTO_CONNECT_SPOT_MESH, CHANNEL_NAME);
    configureTlsClient(sharedNode, options.transport);
    sharedDiscovery.connectRegistry(options.endpoint);
    sharedNode.attachDiscovery(sharedDiscovery);
    applySpotNodeAdmission(sharedNode);
    const dataEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-client-${process.pid}`);
    trace(`shared-node endpoint=${dataEndpoint}`);
    sharedNode.bind(dataEndpoint);
    trace('shared-node bound');
    const spotCount = Math.min(Math.max(1, Math.trunc(options.clients)), 1);
    for (let i = 0; i < spotCount; i += 1) {
      trace(`slot-${i} create-spot`);
      const spot = sharedNode.createSpot();
      spot.setSubscription(TOPIC);
      slots.push({ spot });
    }

    const drainSlots = () => {
      let processed = false;
      for (let i = 0; i < slots.length; i += 1) {
        const { spot } = slots[i];
        if (drainSpot(spot, (received) => {
          const header = decodeMetricHeaderFromParts(received.parts);
          if (!header) {
            return;
          }
          if ((header.runId >>> 0) !== createRunId(1) || (header.msgSize >>> 0) !== options.msgSize) {
            return;
          }
          if (header.phase === 0) {
            readySeen.add(i);
            return;
          }
          if (header.phase === 2) {
            cooldownSeen.add(i);
            return;
          }
          if (!collectActive || currentEpochNs() > activeStopNs) {
            return;
          }
          collector?.record(header, currentEpochNs());
        })) {
          processed = true;
        }
      }
      return processed;
    };

    const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
    while (Date.now() < stabilizationDeadline) {
      drainSlots();
      tryControlPublish(controlPub, 'CONNECTED');
      await sleepImmediate();
    }
    const readyDeadline = Date.now() + 20_000;
    let lastAdvertiseMs = 0;
    while (Date.now() < readyDeadline && readySeen.size < slots.length) {
      const nowMs = Date.now();
      if (nowMs - lastAdvertiseMs >= 50) {
        tryControlPublish(controlPub, 'CONNECTED');
        lastAdvertiseMs = nowMs;
      }
      if (!drainSlots()) {
        await sleepImmediate();
      }
    }
    if (readySeen.size < slots.length) {
      throw new Error(`spot warmup readiness timeout ${readySeen.size}/${slots.length}`);
    }
    trace(`warmup-ready count=${readySeen.size}`);
    for (;;) {
      if (tryControlPublish(controlPub, 'CONNECTED')) {
        break;
      }
      await controlPubWaiter.wait(POLLOUT);
    }
    const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
    while (Date.now() < controlSettleDeadline) {
      await sleepImmediate();
    }
    for (;;) {
      if (tryControlPublish(controlPub, `READY_COUNT,${options.msgSize},${options.clients}`)) {
        break;
      }
      await controlPubWaiter.wait(POLLOUT);
    }
    console.log(`CLIENT_READY,${options.msgSize}`);
    trace('client-ready');

    rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
    let startRequested = false;
    let startBroadcast = false;
    (async () => {
      for await (const line of rl) {
        if (line === `START,${options.msgSize}`) {
          startRequested = true;
        }
      }
    })();

    while (!startRequested) {
      let drained = false;
      while (true) {
        const received = subscribeNoWait(controlSub);
        if (!received) {
          break;
        }
        drained = true;
        const payloadText = received.parts[0].data().toString('utf8');
        if (payloadText === `START,${options.msgSize}`) {
          startBroadcast = true;
        }
      }
      if (!startRequested && !drained) {
        drainSlots();
        await sleepImmediate();
      }
    }
    trace(`start-handshake-done runner=${startRequested} broadcast=${startBroadcast}`);

    const activeStartNs = currentEpochNs();
    activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    const idleStopNs = activeStopNs + 2_000_000_000n;
    collector = createMetricCollector({
      runId: createRunId(1),
      msgSize: options.msgSize,
      activeStartNs,
      activeStopNs,
      sampleCap: resolveMultiLatencySampleCap()
    });
    collectActive = true;
    trace('dispatch-ready');
    while (currentEpochNs() < idleStopNs) {
      if (currentEpochNs() >= activeStopNs && cooldownSeen.size >= slots.length) {
        break;
      }
      if (!drainSlots()) {
        await sleepImmediate();
      }
    }
    collectActive = false;
    trace(`drain-complete cooldown=${cooldownSeen.size}`);
    const result = collector ? await collector.finish() : { latenciesNs: [] };
    for (const metricLine of summarizeMetrics(
      'MULTI_SPOT',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      'current',
      result.accepted
    )) {
      console.log(metricLine);
    }
    trace('result-flushed');
    await new Promise((resolve) => process.stdout.write('', resolve));
    process.exit(0);
  } finally {
    rl?.close();
    controlSubWaiter.close();
    controlPubWaiter.close();
    closeQuietly(controlSub);
    closeQuietly(controlPub);
    for (const slot of slots) {
      closeQuietly(slot.spot);
    }
    closeQuietly(sharedDiscovery);
    closeQuietly(sharedNode);
    closeQuietly(ctx);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
