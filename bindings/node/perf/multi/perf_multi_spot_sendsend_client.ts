// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('@zlink-systems/zlink');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  currentEpochNs,
  decodeMetricHeaderFromParts,
  sleepImmediate,
  summarizeMetrics,
  stampPayload
} = require('../common/perf_metrics');
const { configureTlsClient, configureTlsServer } = require('../common/perf_tls');
const {
  benchmarkEndpoint,
  parseMultiArgs,
  resolveMultiSpotControlSettleMs,
  resolveMultiSpotReadySettleMs
} = require('./perf_multi_common');
const {
  POLLIN,
  POLLOUT,
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySocketPolicy,
  applySpotNodeAdmission,
  createSocketEventWaiter,
  emitMultiSocketHwmDetail,
  publishControlUntilSent,
  waitForControlStart,
  waitForRunnerControlConnected,
  waitForRunnerStart
} = require('./perf_multi_runtime');

const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.fromBytes(
  Buffer.from('PERF_SPOT_SENDSEND_NODE', 'ascii')
);
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.fromBytes(
  Buffer.from('PERF_SPOT_SENDSEND_SPOT', 'ascii')
);

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch (err) {
    console.error(`[multi-spot-sendsend-client] close failed: ${err}`);
  }
}

function tryRecvRouted(spot) {
  try {
    return spot.recvRouted(zlink.RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

function trySpotSend(spot, payload) {
  try {
    return spot.sendToSpot(SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID)
      .message(payload)
      .flags(zlink.SendFlags.DontWait)
      .submit();
  } catch (error) {
    if (error instanceof zlink.SubmitError &&
        error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    const text = String(error && error.message ? error.message : error);
    if ((error && error.code === 'EAGAIN') ||
        /Resource temporarily unavailable|temporarily unavailable|would block/i.test(text)) {
      return false;
    }
    throw error;
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = new zlink.Context();
  applyContextPolicy(ctx, 'client', 'MULTI_SPOT_SENDSEND');
  const controlPub = new zlink.PubSocket(ctx);
  const controlSub = new zlink.SubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
  const node = new zlink.SpotNode(ctx);
  const slots = [];

  try {
    applySocketPolicy(controlPub);
    applySocketPolicy(controlSub);
    applyAutoHwmMsgUnit(controlPub, options.msgSize);
    applyAutoHwmMsgUnit(controlSub, options.msgSize);
    applySpotNodeAdmission(node);
    controlPub.bind(options.controlEndpoint);
    console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
    controlSub.setSubscription(CONTROL_TOPIC);
    controlSub.connect(options.serverControlEndpoint);
    await waitForRunnerControlConnected();

    node.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from('PERF_SPOT_SENDSEND_CLIENT_NODE', 'ascii')));
    const dataEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-sendsend-client-${process.pid}`);
    configureTlsServer(node, options.transport);
    configureTlsClient(node, options.transport);
    node.bind(dataEndpoint);
    node.connectPeer(options.peerEndpoint);
    for (let i = 0; i < options.clients; i += 1) {
      const spot = node.createSpot();
      spot.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(`PERF_SPOT_SENDSEND_CLIENT_SPOT_${i}`, 'ascii')));
      slots.push({
        spot,
        payload: createPayload(options.msgSize),
        inflight: false
      });
    }
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);

    const stabilizationDeadline = Date.now() + resolveMultiSpotReadySettleMs();
    while (Date.now() < stabilizationDeadline) {
      await sleepImmediate();
    }
    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `DATA_ENDPOINT,${dataEndpoint}`);
    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, 'CONNECTED');
    const controlSettleDeadline = Date.now() + resolveMultiSpotControlSettleMs();
    while (Date.now() < controlSettleDeadline) {
      await sleepImmediate();
    }
    await publishControlUntilSent(
      controlPub,
      controlPubWaiter,
      CONTROL_TOPIC,
      `READY_COUNT,${options.msgSize},${slots.length}`
    );
    console.log(`CLIENT_READY,${options.msgSize}`);

    await waitForRunnerStart(options.msgSize);
    await waitForControlStart(controlSub, controlSubWaiter, options.msgSize);

    const runId = createRunId(1);
    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    const collector = createMetricCollector({
      runId,
      msgSize: options.msgSize,
      activeStartNs,
      activeStopNs,
      roundTrip: true,
    });
    let seq = 1n;
    const poller = new zlink.Poller();
    for (let i = 0; i < slots.length; i += 1) {
      poller.add(slots[i].spot, [POLLIN, POLLOUT], i);
    }

    const drainReplies = () => {
      let progressed = false;
      for (const slot of slots) {
        while (true) {
          const received = tryRecvRouted(slot.spot);
          if (!received) {
            break;
          }
          try {
            slot.inflight = false;
            collector.record(decodeMetricHeaderFromParts(received.parts), currentEpochNs());
            progressed = true;
          } finally {
            received.close();
          }
        }
      }
      return progressed;
    };

    try {
      while (currentEpochNs() < activeStopNs) {
        let progressed = drainReplies();
        for (const slot of slots) {
          if (slot.inflight) {
            continue;
          }
          stampPayload(slot.payload, { phase: 1, runId, msgSize: options.msgSize, seq });
          if (trySpotSend(slot.spot, slot.payload)) {
            slot.inflight = true;
            seq += 1n;
            progressed = true;
          }
        }
        if (!progressed) {
          poller.waitMany(Math.max(1, poller.size), -1);
        }
      }
      while (slots.some((slot) => slot.inflight)) {
        if (!drainReplies()) {
          poller.waitMany(Math.max(1, poller.size), -1);
        }
      }
    } finally {
      poller.close();
    }

    const result = await collector.finish();
    for (const metricLine of summarizeMetrics(
      'MULTI_SPOT_SENDSEND',
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
    controlPubWaiter.close();
    controlSubWaiter.close();
    closeQuietly(controlSub);
    closeQuietly(controlPub);
    for (const slot of slots) {
      closeQuietly(slot.spot);
    }
    closeQuietly(node);
    closeQuietly(ctx);
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
