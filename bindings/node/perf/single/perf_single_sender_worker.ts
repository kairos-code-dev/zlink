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

// C parity: bindings/c/perf/single/common/perf_single_one_way.hpp
// send_socket_active_message (~145-166) sends with ZLINK_DONTWAIT and,
// with retry_on_eagain_=true, returns send_step_retry on EAGAIN /
// EWOULDBLOCK / ETIMEDOUT / EINTR so send_active_samples (~168-196) loops
// (the blocking-sender semantic = retry through backpressure, NEVER a
// thrown failure or silent drop). The previous blocking `.submit()` here
// threw `Resource temporarily unavailable` on slow transports (ws/wss/tls
// with the C-mandated 200ms SNDTIMEO), aborting the worker and dead-
// locking the main thread's synchronous `-1` poller drain. PERF_POLICY
// § 1.1.2 / PERF_SINGLE_TEST_POLICY § 1.4.
function isTransientSubmit(error) {
  const text = String(error && error.message ? error.message : error);
  return (error instanceof zlink.SubmitError
      && (error.result === zlink.SubmitResult.Backpressured
        || error.result === zlink.SubmitResult.NotConnected
        || error.result === zlink.SubmitResult.NotFound))
    || (error && error.code === 'EAGAIN')
    || /Resource temporarily unavailable|temporarily unavailable|would block|timed out|Host unreachable|not connected/i.test(text);
}

function submitOnce(kind, socket, body, receiverRoutingId, topic) {
  if (kind === 'pubsub') {
    return socket.publish(topic).message(body)
      .flags(zlink.SendFlags.DontWait).submit();
  }
  if (kind === 'router_router') {
    return socket.send(receiverRoutingId).message(body)
      .flags(zlink.SendFlags.DontWait).submit();
  }
  return socket.send().message(body).flags(zlink.SendFlags.DontWait).submit();
}

// Retry through transient backpressure until accepted (C send_step_retry
// loop). Returns when the message is on the wire; throws only on a real
// fatal error. `deadlineNs` (optional) bounds the active-sample retry the
// same way C's send_active_samples is bounded by the duration deadline.
function submitWithRetry(kind, socket, body, receiverRoutingId, topic, deadlineNs) {
  for (;;) {
    try {
      if (submitOnce(kind, socket, body, receiverRoutingId, topic)) {
        return true;
      }
    } catch (error) {
      if (!isTransientSubmit(error)) {
        throw error;
      }
    }
    if (deadlineNs !== undefined && process.hrtime.bigint() >= deadlineNs) {
      return false;
    }
  }
}

function sendStopToken(kind, socket, receiverRoutingId, topic) {
  // PERF_SINGLE_TEST_POLICY § 1.4 / C send_stop_token_with_retry
  // (~202-215): emit the wire-level stop token once, retrying through
  // transient backpressure so the terminator always reaches the peer.
  trace(`sendStopToken begin kind=${kind}`);
  submitWithRetry(kind, socket, STOP_TOKEN_BYTES, receiverRoutingId, topic);
  trace(`sendStopToken sent kind=${kind}`);
}

function sendLoop(kind, socket, payload, duration, runId, msgSize, seqStart, receiverRoutingId, topic) {
  const activeStopNs = process.hrtime.bigint() + BigInt(Math.floor(duration * 1_000_000_000));
  let seq = seqStart;
  while (process.hrtime.bigint() < activeStopNs) {
    stampPayload(payload, { phase: 1, runId, msgSize, seq });
    // C send_active_samples: a retried (backpressured) send does not
    // advance seq until it is actually accepted; the duration deadline
    // bounds the retry so we never block past the active window.
    if (submitWithRetry(kind, socket, payload, receiverRoutingId, topic, activeStopNs)) {
      seq += 1n;
    }
  }
  stampPayload(payload, { phase: 2, runId, msgSize, seq });
  submitWithRetry(kind, socket, payload, receiverRoutingId, topic);
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
        applyAutoHwmMsgUnit(socket, msgSize);
        socket.setRoutingId(zlink.RoutingId.fromBytes(Buffer.from(senderRoutingIdBytes)));
        ctx.recalculateAutoHwm();
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

    // PERF_SINGLE_TEST_POLICY § 2.0.1 / C perf_single_one_way.hpp
    // run_active_phase: single must not add a start/stop control channel.
    // The connection-ready gate is the only cross-thread sync before the
    // active window; phase end is the wire stop token alone.
    //
    // PUBSUB binds in this worker, so the subscriber must connect before
    // we publish: the main thread replies `ready` once CONNECTION_READY +
    // post-ready settle have completed (mirrors C `setup_connected_pubsub_
    // pair` ordering). All other patterns connect from this worker, so the
    // connection-ready gate is satisfied here and we proceed directly.
    if (kind === 'pubsub') {
      trace('waiting ready');
      await waitForCommand(port, 'ready');
      trace('ready received');
    } else {
      port.postMessage({ type: 'ready' });
    }

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
    // C perf_single_one_way.hpp: the sender thread joins after emitting the
    // wire stop token. No `done`/`stop` ack — the worker exits and the
    // receiver loop already terminates on the wire stop token.
    trace('sender done');
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
