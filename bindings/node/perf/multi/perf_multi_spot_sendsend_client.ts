// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('@zlink-systems/zlink');
const {
  createMetricCollector,
  createPayload,
  createRunId,
  currentEpochNs,
  decodeMetricHeader,
  sleepMillis,
  summarizeMetrics,
  stampPayload,
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
  pollEvents,
  publishControlUntilSent,
  subscribeNoWait,
  trySocketPublish,
  waitForSpotNodeConnectedPeerCount,
  waitForRunnerControlConnected,
  waitForRunnerStart
} = require('./perf_multi_runtime');

const CONTROL_TOPIC = 'bench';
const SERVER_NODE_ROUTING_ID = zlink.RoutingId.from(
  Buffer.from('PERF_SPOT_SENDSEND_NODE', 'ascii')
);
const SERVER_SPOT_ROUTING_ID = zlink.RoutingId.from(
  Buffer.from('PERF_SPOT_SENDSEND_SPOT', 'ascii')
);
const TRACE = process.env.PERF_MULTI_SPOT_SENDSEND_TRACE === '1';

function trace(message) {
  if (TRACE) {
    console.error(`[multi-spot-sendsend-client] ${message}`);
  }
}

function isTransientSendError(error) {
  return error instanceof zlink.SubmitError
    && (error.result === zlink.SubmitResult.Backpressured
        || error.result === zlink.SubmitResult.NotConnected
        || error.result === zlink.SubmitResult.NotFound
        || error.result === zlink.SubmitResult.NotAdmitted);
}

function sendToServer(slot) {
  try {
    return slot.spot.sendToSpot(SERVER_NODE_ROUTING_ID, SERVER_SPOT_ROUTING_ID)
      .message(slot.payload)
      .flags(zlink.SendFlags.DontWait)
      .submit();
  } catch (error) {
    if (isTransientSendError(error)) {
      return false;
    }
    throw error;
  }
}

function activeSpotSlotLimit(totalSlots, msgSize) {
  if (msgSize >= 131072) {
    return Math.min(totalSlots, 8);
  }
  if (msgSize >= 65536) {
    return Math.min(totalSlots, 32);
  }
  return totalSlots;
}

function closeQuietly(resource) {
  try {
    resource?.close();
  } catch (err) {
    console.error(`[multi-spot-sendsend-client] close failed: ${err}`);
  }
}

function receiveControlStart(controlSub, msgSize) {
  const received = subscribeNoWait(controlSub);
  if (!received) {
    return false;
  }
  try {
    return received.parts[0].data().toString('utf8') === `START,${msgSize}`;
  } finally {
    received.close();
  }
}

async function main() {
  const options = parseMultiArgs(process.argv.slice(2));
  const ctx = zlink.createContext();
  applyContextPolicy(ctx, 'client', 'MULTI_SPOT_SENDSEND');
  const controlPub = zlink.createPubSocket(ctx);
  const controlSub = zlink.createSubSocket(ctx);
  const controlPubWaiter = createSocketEventWaiter(controlPub, POLLOUT);
  const controlSubWaiter = createSocketEventWaiter(controlSub, POLLIN);
  const node = zlink.createSpotNode(ctx);
  const slots = [];
  const poller = zlink.createPoller();
  let pollBuffer = null;

  try {
    applySocketPolicy(controlPub);
    applySocketPolicy(controlSub);
    applyAutoHwmMsgUnit(ctx, options.msgSize);
    applySpotNodeAdmission(node);
    controlPub.bind(options.controlEndpoint);
    console.log(`CLIENT_CONTROL_ENDPOINT,${options.controlEndpoint}`);
    controlSub.setSubscription(CONTROL_TOPIC);
    controlSub.connect(options.serverControlEndpoint);
    await waitForRunnerControlConnected();
    trace('control-connected');

    node.setRoutingId(zlink.RoutingId.from(Buffer.from('PERF_SPOT_SENDSEND_CLIENT_NODE', 'ascii')));
    const dataEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-sendsend-client-${process.pid}`);
    const dataRouterEndpoint = await benchmarkEndpoint(options.transport, `multi-spot-sendsend-client-router-${process.pid}`);
    configureTlsServer(node, options.transport);
    configureTlsClient(node, options.transport);
    node.setRouterBind(dataRouterEndpoint);
    node.setPubBind(dataEndpoint);
    node.connectPeer(options.peerEndpoint);
    for (let i = 0; i < options.clients; i += 1) {
      const spot = node.createSpot();
      spot.setRoutingId(zlink.RoutingId.from(Buffer.from(`PERF_SPOT_SENDSEND_CLIENT_SPOT_${i}`, 'ascii')));
      slots.push({
        spot,
        payload: createPayload(options.msgSize),
        waitingReply: false,
        nextSeq: 1n
      });
    }
    ctx.recalculateAutoHwm();
    emitMultiSocketHwmDetail(controlPub, 'spotnode_control_pub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(controlSub, 'spotnode_control_sub', options.transport, options.msgSize);
    emitMultiSocketHwmDetail(node, 'spotnode_data', options.transport, options.msgSize);

    await sleepMillis(resolveMultiSpotReadySettleMs());
    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, `DATA_ENDPOINT,${dataEndpoint}`);
    await sleepMillis(resolveMultiSpotControlSettleMs());
    await waitForSpotNodeConnectedPeerCount(node, 1);
    await publishControlUntilSent(controlPub, controlPubWaiter, CONTROL_TOPIC, 'CONNECTED');
    await sleepMillis(resolveMultiSpotControlSettleMs());
    await publishControlUntilSent(
      controlPub,
      controlPubWaiter,
      CONTROL_TOPIC,
      `READY_COUNT,${options.msgSize},${slots.length}`
    );
    console.log(`CLIENT_READY,${options.msgSize}`);
    trace('client-ready');

    // C parity: wait_msg_size_start_with_ready_republish — re-publish
    // READY_COUNT every 250ms while waiting for START (stdin or control)
    // so the slow-joiner control link can never wedge the handshake now
    // that publishControlUntilSent is bounded (returns on timeout).
    let runnerStarted = false;
    let controlStarted = false;
    const startFromRunner = waitForRunnerStart(options.msgSize).then(() => {
      runnerStarted = true;
    });
    let nextReadyAt = Date.now();
    while (!runnerStarted || !controlStarted) {
      if (Date.now() >= nextReadyAt) {
        trySocketPublish(controlPub, CONTROL_TOPIC, `DATA_ENDPOINT,${dataEndpoint}`);
        trySocketPublish(controlPub, CONTROL_TOPIC, 'CONNECTED');
        trySocketPublish(controlPub, CONTROL_TOPIC, `READY_COUNT,${options.msgSize},${slots.length}`);
        nextReadyAt = Date.now() + 250;
      }
      controlStarted = controlStarted || receiveControlStart(controlSub, options.msgSize);
      await Promise.race([
        startFromRunner,
        sleepMillis(Math.min(50, Math.max(1, nextReadyAt - Date.now())))
      ]);
    }
    trace('start-ready');

    const runId = createRunId(1);
    const activeSlots = slots.slice(0, activeSpotSlotLimit(slots.length, options.msgSize));
    pollBuffer = zlink.createPollEvents(Math.max(1, activeSlots.length));
    for (let i = 0; i < activeSlots.length; i += 1) {
      poller.add(activeSlots[i].spot, pollEvents(POLLIN), i);
    }
    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs + BigInt(Math.floor(options.duration * 1_000_000_000));
    const collector = createMetricCollector({
      runId,
      msgSize: options.msgSize,
      activeStartNs,
      activeStopNs,
      roundTrip: true,
    });

    const drainSlot = (index) => {
      const slot = activeSlots[index];
      let progressed = false;
      const received = new zlink.Received();
      while (true) {
        try {
          if (!slot.spot.recvRouted(received, zlink.RecvFlags.DontWait)) {
            break;
          }
        } catch (error) {
          if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
            break;
          }
          throw error;
        }
        try {
          if (!received.routingId || !received.spotRid || received.requestSeq !== 0n
              || received.parts.length === 0) {
            continue;
          }
          slot.waitingReply = false;
          collector.record(
            decodeMetricHeader(received.parts[0].data()),
            currentEpochNs()
          );
          progressed = true;
        } finally {
          received.close();
        }
      }
      return progressed;
    };

    while (currentEpochNs() < activeStopNs) {
      let progressed = false;
      let hasWaitingReply = false;
      for (const slot of activeSlots) {
        if (slot.waitingReply) {
          hasWaitingReply = true;
          continue;
        }
        stampPayload(slot.payload, {
          phase: 1,
          runId,
          msgSize: options.msgSize,
          seq: slot.nextSeq
        });
        if (sendToServer(slot)) {
          slot.waitingReply = true;
          slot.nextSeq += 1n;
          progressed = true;
          hasWaitingReply = true;
        }
      }
      for (let i = 0; i < activeSlots.length; i += 1) {
        progressed = drainSlot(i) || progressed;
      }
      if (progressed) {
        continue;
      }
      const remainingMs = Number((BigInt(activeStopNs - currentEpochNs())) / 1_000_000n);
      if (remainingMs <= 0) {
        break;
      }
      const readyCount = poller.wait(pollBuffer, Math.min(50, Math.max(1, remainingMs)));
      for (let offset = 0; offset < readyCount; offset += 1) {
        const index = pollBuffer.slot(offset);
        if (Number.isInteger(index) && index >= 0 && index < activeSlots.length) {
          drainSlot(index);
        }
      }
      if (!hasWaitingReply && readyCount === 0) {
        await sleepMillis(1);
      }
    }
    const drainDeadline = Date.now() + 1000;
    while (activeSlots.some((slot) => slot.waitingReply) && Date.now() < drainDeadline) {
      let progressed = false;
      for (let i = 0; i < activeSlots.length; i += 1) {
        progressed = drainSlot(i) || progressed;
      }
      if (progressed) {
        continue;
      }
      const readyCount = poller.wait(pollBuffer, 50);
      for (let offset = 0; offset < readyCount; offset += 1) {
        const index = pollBuffer.slot(offset);
        if (Number.isInteger(index) && index >= 0 && index < activeSlots.length) {
          drainSlot(index);
        }
      }
    }

    const result = await collector.finish();
    trace(`finish accepted=${result.accepted}`);
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
    pollBuffer?.close();
    poller.close();
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
