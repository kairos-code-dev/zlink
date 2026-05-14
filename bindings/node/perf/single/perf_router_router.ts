// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('@zlink-systems/zlink');
const {
  createPayload,
  createMetricCollector,
  createRunId,
  decodeMetricHeaderFromParts,
  currentEpochNs,
  summarizeMetrics,
  stampPayload,
} = require('../common/perf_metrics');
const {
  applyContextPolicy,
  applyAutoHwmMsgUnit,
  applySocketPolicy,
  benchmarkEndpoint,
  configureTlsClient,
  configureTlsServer,
  emitSingleSocketHwmDetail,
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  waitForMonitorConnectionReady,
} = require('./perf_single_common');
const { isStopTokenParts, STOP_TOKEN_BYTES } = require('../perf_stop_token');

const RECEIVER_ID = Buffer.from('router-perf-receiver', 'ascii');
const SENDER_ID = Buffer.from('router-perf-sender', 'ascii');
const RECEIVER_ROUTING_ID = zlink.RoutingId.fromBytes(RECEIVER_ID);
const SENDER_ROUTING_ID = zlink.RoutingId.fromBytes(SENDER_ID);

function trace(message) {
  if (process.env.PERF_NODE_TRACE === '1') {
    console.error(`[router-router] ${message}`);
  }
}

function partStrings(received) {
  return received.parts.map((part) => part.data().toString());
}

function recvNoWait(socket) {
  const received = new zlink.Received();
  try {
    return socket.recv(received, zlink.RecvFlags.DontWait) ? received : null;
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === zlink.RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

function transientSubmitError(error) {
  const text = String(error && error.message ? error.message : error);
  return (error instanceof zlink.SubmitError
      && (error.result === zlink.SubmitResult.Backpressured
        || error.result === zlink.SubmitResult.NotConnected
        || error.result === zlink.SubmitResult.NotFound))
    || (error && error.code === 'EAGAIN')
    || text.includes('Resource temporarily unavailable')
    || text.includes('Host unreachable')
    || text.includes('Transport endpoint is not connected');
}

function sendRouter(sender, target, payload, flags = zlink.SendFlags.DontWait) {
  try {
    return sender.send(target).message(payload).flags(flags).submit();
  } catch (error) {
    if (transientSubmitError(error)) {
      return false;
    }
    throw error;
  }
}

function sendRouterStopToken(sender, target) {
  for (let retry = 0; retry < 100; retry += 1) {
    if (sendRouter(sender, target, STOP_TOKEN_BYTES, zlink.SendFlags.None)) {
      return;
    }
  }
  throw new Error('router-router stop token send failed');
}

function drainRouter(receiver, collector) {
  while (true) {
    const received = recvNoWait(receiver);
    if (!received) {
      return false;
    }
    if (isStopTokenParts(received.parts)) {
      return true;
    }
    const header = decodeMetricHeaderFromParts(received.parts);
    collector.record(header, currentEpochNs());
  }
}

function handshakeRouters(receiver, sender) {
  sender.send(RECEIVER_ROUTING_ID).message(Buffer.from('PING')).submit();
  const ping = new zlink.Received();
  receiver.recv(ping);
  if (ping.routingId === null || partStrings(ping).join(',') !== 'PING') {
    throw new Error('router-router handshake receive failed');
  }

  receiver.send(ping.routingId).message(Buffer.from('PONG')).submit();
  const pong = new zlink.Received();
  sender.recv(pong);
  if (pong.routingId === null || partStrings(pong).join(',') !== 'PONG') {
    throw new Error('router-router handshake reply failed');
  }
  return pong.routingId;
}

async function runRouterRouterBenchmark(msgSize, options) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const receiver = new zlink.RouterSocket(ctx);
  const sender = new zlink.RouterSocket(ctx);
  const receiverMonitor = receiver.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
  const senderMonitor = sender.monitorOpen([zlink.MonitorEventType.ConnectionReady]);
  const endpoint = await benchmarkEndpoint(options.transport, `router-router-${msgSize}`);

  try {
    applySocketPolicy(receiver, options);
    applySocketPolicy(sender, options);
    applyAutoHwmMsgUnit(receiver, msgSize);
    applyAutoHwmMsgUnit(sender, msgSize);
    ctx.recalculateAutoHwm();
    receiver.setRoutingId(RECEIVER_ROUTING_ID);
    sender.setRoutingId(SENDER_ROUTING_ID);
    configureTlsServer(receiver, options.transport);
    configureTlsClient(sender, options.transport);
    receiver.bind(endpoint);
    sender.connect(endpoint);
    await waitForMonitorConnectionReady(receiverMonitor);
    await waitForMonitorConnectionReady(senderMonitor);
    const targetRoutingId = handshakeRouters(receiver, sender);
    trace('handshake done');

    const activeStartNs = currentEpochNs();
    const activeStopNs = activeStartNs
      + BigInt(Math.floor(options.duration * 1_000_000_000));
    const runId = createRunId(options.runId ?? 1);
    const payload = createPayload(msgSize);
    const collector = createMetricCollector({
      runId,
      msgSize,
      activeStartNs,
      activeStopNs,
      sampleCap: resolveSingleLatencySampleCap()
    });

    let seq = 1n;
    while (currentEpochNs() < activeStopNs) {
      stampPayload(payload, { phase: 1, runId, msgSize, seq });
      if (sendRouter(sender, targetRoutingId, payload)) {
        seq += 1n;
      }
      drainRouter(receiver, collector);
    }
    stampPayload(payload, { phase: 2, runId, msgSize, seq });
    sendRouter(sender, targetRoutingId, payload);
    sendRouterStopToken(sender, targetRoutingId);
    while (!drainRouter(receiver, collector)) {
      // Drain until the wire-level stop token arrives.
    }
    const result = collector.finish();
    emitSingleSocketHwmDetail(receiver, 'ROUTER_ROUTER', options.transport, 'receiver', msgSize);
    emitSingleSocketHwmDetail(sender, 'ROUTER_ROUTER', options.transport, 'sender', msgSize);
    return result;
  } finally {
    trace('closing');
    receiverMonitor.close();
    senderMonitor.close();
    receiver.close();
    sender.close();
    trace('receiver closed');
    ctx.close();
    trace('ctx closed');
  }
}

module.exports = { runRouterRouterBenchmark };

if (require.main === module) {
  (async () => {
    const options = parseSingleBinaryArgs(process.argv.slice(2));
    const result = await runRouterRouterBenchmark(options.msgSize, options);
    for (const line of summarizeMetrics(
      'ROUTER_ROUTER',
      options.transport,
      options.msgSize,
      result.latenciesNs,
      options.duration,
      options.libName,
      result.accepted
    )) {
      console.log(line);
    }
  })().catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
}
