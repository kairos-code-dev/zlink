// SPDX-License-Identifier: MPL-2.0

'use strict';

const { parentPort, workerData } = require('node:worker_threads');
const zlink = require('@zlink-systems/zlink');
const {
  createPayload,
  stampPayload
} = require('../common/perf_metrics');
const {
  applyContextPolicy,
  applyAutoHwmMsgUnit,
  applySocketPolicy,
  configureTlsClient,
  configureTlsServer,
  emitSingleSocketHwmDetail,
  waitForConnectionReady,
} = require('./perf_single_common');
const { STOP_TOKEN_BYTES } = require('../perf_stop_token');

const DEFAULT_TOPIC = 'perf.topic';

function ensureParentPort() {
  if (!parentPort) {
    throw new Error('sender worker requires parentPort');
  }
  return parentPort;
}

function trace(message) {
  if (process.env.PERF_NODE_TRACE === '1') {
    console.error(`[sender-worker] ${message}`);
  }
}

function waitForCommand(port, type) {
  return new Promise((resolve) => {
    const handler = (message) => {
      if (!message || message.type !== type) {
        return;
      }
      port.off('message', handler);
      resolve(message);
    };
    port.on('message', handler);
  });
}

async function connectSender(kind, socket, endpoint, transport) {
  configureTlsClient(socket, transport);
  await waitForConnectionReady(socket, () => socket.connect(endpoint));
}

async function handshakeRouterSender(port, sender, receiverRoutingId) {
  port.postMessage({ type: 'connected' });
  await waitForCommand(port, 'handshake');
  sender.send(receiverRoutingId).message(Buffer.from('PING')).submit();
  const reply = new zlink.Received();
  sender.recv(reply);
  try {
    const text = reply.parts.map((part) => part.data().toString()).join(',');
    if (text !== 'PONG') {
      throw new Error('router-router handshake reply failed');
    }
    if (reply.routingId) {
      return reply.routingId;
    }
    return receiverRoutingId;
  } finally {
    reply.close();
  }
}

function sendStopToken(kind, socket, receiverRoutingId, topic) {
  // PERF_SINGLE_TEST_POLICY § 1.4: emit the wire-level stop token once at
  // phase end. Use blocking send (no DontWait) so transient backpressure
  // is absorbed by the socket; bound retries to avoid hanging if the peer
  // has gone away. For pubsub the publisher is configured with
  // `no_drop=true` (see `perf_pubsub.ts`), so the sentinel cannot be
  // silently discarded by XPUB.
  trace(`sendStopToken begin kind=${kind}`);
  for (let retry = 0; retry < 100; retry += 1) {
    try {
      if (kind === 'pubsub') {
        socket.publish(topic).message(STOP_TOKEN_BYTES).submit();
      } else if (kind === 'router_router') {
        socket.send(receiverRoutingId).message(STOP_TOKEN_BYTES).submit();
      } else {
        socket.send().message(STOP_TOKEN_BYTES).submit();
      }
      trace(`sendStopToken sent kind=${kind} retry=${retry}`);
      return;
    } catch (error) {
      const text = String(error && error.message ? error.message : error);
      if (error instanceof zlink.SubmitError
          && error.result === zlink.SubmitResult.Backpressured) {
        continue;
      }
      if ((error && error.code === 'EAGAIN')
          || text.includes('Resource temporarily unavailable')
          || text.includes('Host unreachable')
          || text.includes('Transport endpoint is not connected')) {
        continue;
      }
      throw error;
    }
  }
}

function submitActiveMessage(kind, socket, payload, receiverRoutingId, topic) {
  try {
    if (kind === 'pubsub') {
      socket.publish(topic).message(payload).flags(zlink.SendFlags.DontWait).submit();
    } else if (kind === 'router_router') {
      socket.send(receiverRoutingId).message(payload).flags(zlink.SendFlags.DontWait).submit();
    } else {
      socket.send().message(payload).flags(zlink.SendFlags.DontWait).submit();
    }
    return true;
  } catch (error) {
    const text = String(error && error.message ? error.message : error);
    if (error instanceof zlink.SubmitError
        && (error.result === zlink.SubmitResult.Backpressured
          || error.result === zlink.SubmitResult.NotConnected
          || error.result === zlink.SubmitResult.NotFound)) {
      return false;
    }
    if ((error && error.code === 'EAGAIN')
        || text.includes('Resource temporarily unavailable')
        || text.includes('Host unreachable')
        || text.includes('Transport endpoint is not connected')) {
      return false;
    }
    throw error;
  }
}

function sendLoop(kind, socket, payload, duration, runId, msgSize, seqStart, receiverRoutingId, topic) {
  const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(duration * 1_000_000_000));
  let seq = seqStart;
  while (process.hrtime.bigint() < activeStopNs) {
    stampPayload(payload, { phase: 1, runId, msgSize, seq });
    if (!submitActiveMessage(kind, socket, payload, receiverRoutingId, topic)) {
      continue;
    }
    seq += 1n;
  }
  stampPayload(payload, { phase: 2, runId, msgSize, seq });
  submitActiveMessage(kind, socket, payload, receiverRoutingId, topic);
  sendStopToken(kind, socket, receiverRoutingId, topic);
}

async function main() {
  const port = ensureParentPort();
  const {
    kind,
    transport,
    endpoint,
    duration,
    msgSize,
    runId,
    receiverRoutingIdBytes,
    senderRoutingIdBytes,
    options
  } = workerData;
  const topic = typeof workerData.topic === 'string' && workerData.topic.length > 0
    ? workerData.topic
    : DEFAULT_TOPIC;
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const payload = createPayload(msgSize);
  let socket = null;
  let activeReceiverRoutingId = receiverRoutingIdBytes
    ? zlink.RoutingId.fromBytes(Buffer.from(receiverRoutingIdBytes))
    : null;

  try {
    switch (kind) {
      case 'pair':
        socket = new zlink.PairSocket(ctx);
        applySocketPolicy(socket, options);
        applyAutoHwmMsgUnit(socket, msgSize);
        ctx.recalculateAutoHwm();
        await connectSender(kind, socket, endpoint, transport);
        break;
      case 'dealer_dealer':
        socket = new zlink.DealerSocket(ctx);
        applySocketPolicy(socket, options);
        applyAutoHwmMsgUnit(socket, msgSize);
        ctx.recalculateAutoHwm();
        await connectSender(kind, socket, endpoint, transport);
        break;
      case 'dealer_router':
        socket = new zlink.DealerSocket(ctx);
        applySocketPolicy(socket, options);
        applyAutoHwmMsgUnit(socket, msgSize);
        ctx.recalculateAutoHwm();
        await connectSender(kind, socket, endpoint, transport);
        break;
      case 'pubsub':
        socket = new zlink.PubSocket(ctx);
        applySocketPolicy(socket, options);
        configureTlsServer(socket, transport);
        socket.bind(endpoint);
        trace('pubsub bound');
        port.postMessage({ type: 'bound' });
        break;
      case 'router_router': {
        socket = new zlink.RouterSocket(ctx);
        applySocketPolicy(socket, options);
        socket.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(senderRoutingIdBytes)));
        await connectSender(kind, socket, endpoint, transport);
        activeReceiverRoutingId = await handshakeRouterSender(
          port,
          socket,
          activeReceiverRoutingId
        );
        break;
      }
      default:
        throw new Error(`unsupported sender worker kind: ${kind}`);
    }

    if (kind !== 'pubsub' && kind !== 'router_router') {
      port.postMessage({ type: 'ready' });
    } else if (kind === 'pubsub') {
      trace('waiting ready');
      await waitForCommand(port, 'ready');
      trace('ready received');
    } else {
      port.postMessage({ type: 'ready' });
    }

    trace('waiting start');
    await waitForCommand(port, 'start');
    trace('start received');
    port.postMessage({ type: 'started' });
    trace(`sendLoop begin kind=${kind} duration=${duration} msgSize=${msgSize}`);
    sendLoop(
      kind,
      socket,
      payload,
      duration,
      runId,
      msgSize,
      1n,
      activeReceiverRoutingId,
      topic
    );
    trace('send loop done');
    if (kind === 'pair') {
      emitSingleSocketHwmDetail(socket, 'PAIR', transport, 'sender', msgSize);
    } else if (kind === 'dealer_dealer') {
      emitSingleSocketHwmDetail(socket, 'DEALER_DEALER', transport, 'sender', msgSize);
    } else if (kind === 'dealer_router') {
      emitSingleSocketHwmDetail(socket, 'DEALER_ROUTER', transport, 'sender', msgSize);
    }
    port.postMessage({ type: 'done' });
    trace('waiting stop');
    await waitForCommand(port, 'stop');
    trace('stop received');
  } catch (error) {
    port.postMessage({
      type: 'error',
      message: String(error && error.stack ? error.stack : error)
    });
    process.exitCode = 1;
  } finally {
    try {
      socket?.close();
    } catch (err) {
      console.error(`[perf] close failed: ${err}`);
    }
    try {
      ctx.close();
    } catch (err) {
      console.error(`[perf] close failed: ${err}`);
    }
  }
}

main();
