// SPDX-License-Identifier: MPL-2.0

'use strict';

const readline = require('node:readline');
const zlink = require('@zlink-systems/zlink');
const { configureTlsClient } = require('../common/perf_tls');
const {
  createMetricCollector,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  sleepImmediate,
  summarizeMetrics
} = require('../common/perf_metrics');
const {
  parseMultiArgs,
  resolveMultiSpotControlSettleMs,
  resolveMultiSpotReadySettleMs
} = require('./perf_multi_common');
const {
  POLLOUT,
  applyAutoHwmMsgUnit,
  applySocketPolicy,
  applyContextPolicy,
  applySpotNodeAdmission,
  createSocketEventWaiter,
  emitMultiSocketHwmDetail,
  publishControlUntilSent,
  subscribeNoWait,
  trySocketPublish,
  waitForRunnerControlConnected
} = require('./perf_multi_runtime');

const TOPIC = 'bench';
const CONTROL_TOPIC = 'bench';
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

function connectPeerIfNeeded(node, endpoint) {
  try {
    node.connectPeer(endpoint);
  } catch (error) {
    const text = String(error && error.message ? error.message : error);
    if (!/Device or resource busy|resource busy|already/i.test(text)) {
      throw error;
    }
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_SPOT');
  const controlPub = new zlink.PubSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  const slots = [];
  let sharedNode = null;
  let rl = null;
  let collector = null;
  let activeStopNs = 0n;
  let collectActive = false;
  const cooldownSeen = new Set();

  try {
    applySocketPolicy(controlPub);
    applySocketPolicy(controlSub);
    applyAutoHwmMsgUnit(controlPub, options.msgSize);
    applyAutoHwmMsgUnit(controlSub, options.msgSize);
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
    controlPub.bind(options.controlEndpoint);
    console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
    controlSub.setSubscription(CONTROL_TOPIC);
    controlSub.connect(options.serverControlEndpoint);
    await waitForRunnerControlConnected();
    trace('control-connected');
    trace(`creating-slots count=${options.clients}`);
    sharedNode = new zlink.SpotNode(ctx);
    configureTlsClient(sharedNode, options.transport);
    applySpotNodeAdmission(sharedNode);
    connectPeerIfNeeded(sharedNode, options.peerEndpoint);
    trace('shared-node connected');
    const spotCount = Math.max(1, Math.trunc(options.clients));
    for (let i = 0; i < spotCount; i += 1) {
      trace(`slot-${i} create-spot`);
      const spot = sharedNode.createSpot();
      spot.setSubscription(TOPIC);
      slots.push({ spot });
    }
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(sharedNode, 'spotnode_data', options.transport, options.msgSize);

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
          if (header.phase === 2) {
            cooldownSeen.add(i);
            return;
          }
          if (!collectActive) {
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
    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, 'CONNECTED');
    const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
    while (Date.now() < controlSettleDeadline) {
      await sleepImmediate();
    }
    await publishControlUntilSent(
      controlPub,
      controlPubWaiter,
      CONTROL_TOPIC,
      `READY_COUNT,${options.msgSize},${options.clients}`
    );
    console.log(`CLIENT_READY,${options.msgSize}`);
    trace('client-ready');
    collector = createMetricCollector({
      runId: createRunId(1),
      msgSize: options.msgSize,
      activeStartNs: 0n,
      activeStopNs: BigInt('0xffffffffffffffff'),
    });
    collectActive = true;

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

    while (!startRequested || !startBroadcast) {
      let drained = false;
      tryControlPublish(controlPub, `READY_COUNT,${options.msgSize},${options.clients}`);
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
      if ((!startRequested || !startBroadcast) && !drained) {
        drainSlots();
        await sleepImmediate();
      }
    }
    trace(`start-handshake-done runner=${startRequested} broadcast=${startBroadcast}`);

    activeStopNs = currentEpochNs() + BigInt(Math.floor(options.duration * 1_000_000_000));
    const idleStopNs = activeStopNs + 2_000_000_000n;
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
    controlPubWaiter.close();
    closeQuietly(controlSub);
    closeQuietly(controlPub);
    for (const slot of slots) {
      closeQuietly(slot.spot);
    }
    closeQuietly(sharedNode);
    closeQuietly(ctx);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
