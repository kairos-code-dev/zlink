// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { once } = require('node:events');
const { Worker } = require('node:worker_threads');
const zlink = require('@zlink-systems/zlink');
const {
  MonitorEventType,
  RecvFlags,
  RecvResult
} = zlink;
const {
  createMetricCollector,
  createPayload,
  createRunId,
  currentEpochNs,
  decodeMetricHeaderFromParts,
  MIN_MSG_SIZE,
  integerEnv,
  sleepImmediate,
  stampPayload
} = require('../common/perf_metrics');
const {
  configureTlsClient,
  configureTlsServer,
} = require('../common/perf_tls');
const { isStopTokenParts } = require('../perf_stop_token');
const { STOP_TOKEN_BYTES } = require('../perf_stop_token');
const POLLIN = 1;

function pollEvents(mask) {
  const events = [];
  if ((mask & POLLIN) !== 0) {
    events.push(zlink.PollEventFlag.PollIn);
  }
  return events;
}

async function reservePort() {
  const server = net.createServer();
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  const address = server.address();
  await new Promise((resolve, reject) => {
    server.close((error) => {
      if (error) {
        reject(error);
        return;
      }
      resolve();
    });
  });
  return address.port;
}

async function benchmarkEndpoint(transport, token) {
  if (transport === 'inproc') {
    return `inproc://perf-${token}-${process.pid}`;
  }
  if (transport === 'ipc') {
    return `ipc://${path.join(os.tmpdir(), `zlink-node-perf-${process.pid}-${token}.sock`)}`;
  }
  if (
    transport === 'tcp'
    || transport === 'tls'
    || transport === 'ws'
    || transport === 'wss'
  ) {
    return `${transport}://127.0.0.1:${await reservePort()}`;
  }
  throw new Error(`unsupported single transport: ${transport}`);
}

function applySocketPolicy(socket, options = {}) {
  const manualOverrides =
    integerEnv('PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES', 0) > 0
    || integerEnv('PERF_ALLOW_MANUAL_SOCKET_OVERRIDES', 0) > 0;
  const hwm = Number.isFinite(options.hwm)
    ? options.hwm
    : integerEnv('PERF_SINGLE_HWM', NaN);
  const sendHwm = Number.isFinite(options.sendHwm)
    ? options.sendHwm
    : integerEnv('PERF_SINGLE_SNDHWM', hwm);
  const recvHwm = Number.isFinite(options.recvHwm)
    ? options.recvHwm
    : integerEnv('PERF_SINGLE_RCVHWM', hwm);
  const sendTimeout = Number.isFinite(options.sendTimeoutMs)
    ? options.sendTimeoutMs
    : integerEnv('PERF_SINGLE_SNDTIMEO_MS', 200);
  const recvTimeout = Number.isFinite(options.recvTimeoutMs)
    ? options.recvTimeoutMs
    : integerEnv('PERF_SINGLE_RCVTIMEO_MS', 200);
  const linger = Number.isFinite(options.lingerMs)
    ? options.lingerMs
    : integerEnv('PERF_SINGLE_LINGER_MS', 0);

  if (socket.options) {
    if (manualOverrides) {
      if (Number.isFinite(sendHwm) && sendHwm > 0) {
        socket.options.sendHwm = sendHwm;
      }
      if (Number.isFinite(recvHwm) && recvHwm > 0) {
        socket.options.recvHwm = recvHwm;
      }
    }
    socket.options.sendTimeout = sendTimeout;
    socket.options.recvTimeout = options.recvTimeout ?? recvTimeout;
    socket.options.linger = linger;
    if ('noDrop' in socket.options && options.noDrop !== undefined) {
      socket.options.noDrop = Boolean(options.noDrop);
    }
  }
}

function applyAutoHwmMsgUnit(socket, msgSize) {
  if (msgSize <= 0 || !socket.options) {
    return;
  }
  try {
    socket.options.autoHwmMsgUnitBytes = msgSize;
  } catch (err) {
    // best effort: only raw sockets exposing the option participate.
  }
}

function applySpotNodeAdmission(node, options = {}) {
  const manualOverrides =
    integerEnv('PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES', 0) > 0
    || integerEnv('PERF_ALLOW_MANUAL_SOCKET_OVERRIDES', 0) > 0;
  if (!manualOverrides) {
    return;
  }
  const hwm = Number.isFinite(options.hwm)
    ? options.hwm
    : integerEnv('PERF_SINGLE_HWM', NaN);
  const sendHwm = Number.isFinite(options.sendHwm)
    ? options.sendHwm
    : integerEnv('PERF_SINGLE_SNDHWM', hwm);
  const recvHwm = Number.isFinite(options.recvHwm)
    ? options.recvHwm
    : integerEnv('PERF_SINGLE_RCVHWM', hwm);

  if (Number.isFinite(sendHwm) && sendHwm > 0) {
    node.pubsubHwm = sendHwm;
  }
  if (Number.isFinite(recvHwm) && recvHwm > 0) {
    node.routerHwm = recvHwm;
  }
}

function socketTypeName(socket) {
  if (socket instanceof zlink.PairSocket) return 'pair';
  if (socket instanceof zlink.PubSocket) return 'pub';
  if (socket instanceof zlink.SubSocket) return 'sub';
  if (socket instanceof zlink.DealerSocket) return 'dealer';
  if (socket instanceof zlink.RouterSocket) return 'router';
  if (zlink.StreamSocket && socket instanceof zlink.StreamSocket) return 'stream';
  return 'unknown';
}

function autoHwmRoleName(role) {
  switch (role) {
    case 1: return 'control';
    case 2: return 'routed';
    case 3: return 'fanout';
    case 4: return 'recv_ingress';
    case 5: return 'spot_data';
    case 6: return 'peer_queue';
    case 7: return 'stream';
    default: return 'none';
  }
}

function singleAutoHwmSnapshotVisible(snapshot) {
  return Number(snapshot.autoHwmAppliedSndHwm) > 0
    || Number(snapshot.autoHwmAppliedRcvHwm) > 0
    || BigInt(snapshot.autoHwmEffectiveMessageBytes ?? 0) > 0n
    || BigInt(snapshot.autoHwmSocketMessageSlots ?? 0) > 0n;
}

function emitSingleSocketHwmDetail(socket, pattern, transport, component, msgSize) {
  if (!socket || !pattern || !component) {
    return;
  }
  let monitor = null;
  try {
    monitor = socket.monitorOpen([MonitorEventType.ConnectionReady]);
    const snapshot = monitor.snapshot();
    if (!singleAutoHwmSnapshotVisible(snapshot)) {
      return;
    }
    console.log(
      'AUTO_HWM_DETAIL'
      + `,pattern=${pattern}`
      + `,transport=${transport}`
      + `,component=${component}`
      + `,msg_size=${msgSize}`
      + ',owner=socket'
      + ',owner_id=0'
      + `,socket=${component}`
      + `,socket_type=${socketTypeName(socket)}`
      + `,role=${autoHwmRoleName(snapshot.autoHwmRole)}`
      + `,sndhwm=${snapshot.autoHwmAppliedSndHwm}`
      + `,rcvhwm=${snapshot.autoHwmAppliedRcvHwm}`
      + `,effective_message_bytes=${snapshot.autoHwmEffectiveMessageBytes}`
      + `,effective_sndbuf=${snapshot.autoHwmEffectiveSndBuf}`
      + `,effective_rcvbuf=${snapshot.autoHwmEffectiveRcvBuf}`
      + `,socket_message_slots=${snapshot.autoHwmSocketMessageSlots}`
    );
  } catch (err) {
    // Auto-HWM detail is diagnostic output; keep the benchmark result path primary.
  } finally {
    monitor?.close();
  }
}

function spotSocketOwnerName(owner) {
  if (zlink.SpotNodeSocketOwner && owner === zlink.SpotNodeSocketOwner.Node) {
    return 'node';
  }
  if (zlink.SpotNodeSocketOwner && owner === zlink.SpotNodeSocketOwner.Spot) {
    return 'spot';
  }
  return 'unknown';
}

function spotSocketTypeName(socketType) {
  if (!zlink.SocketType) {
    return 'unknown';
  }
  switch (socketType) {
    case zlink.SocketType.Pair: return 'pair';
    case zlink.SocketType.Pub: return 'pub';
    case zlink.SocketType.Sub: return 'sub';
    case zlink.SocketType.Dealer: return 'dealer';
    case zlink.SocketType.Router: return 'router';
    case zlink.SocketType.XPub: return 'xpub';
    case zlink.SocketType.XSub: return 'xsub';
    case zlink.SocketType.Stream: return 'stream';
    default: return 'unknown';
  }
}

function emitSingleSpotHwmDetail(node, component, transport, msgSize) {
  if (!node || !component) {
    return;
  }
  let entries = [];
  try {
    entries = node.internalSocketsSnapshot();
  } catch (err) {
    return;
  }
  for (const entry of entries) {
    if (!entry.autoHwmVisible) {
      continue;
    }
    const snapshot = entry.snapshot;
    if (Number(snapshot.autoHwmAppliedSndHwm) <= 0
        && Number(snapshot.autoHwmAppliedRcvHwm) <= 0) {
      continue;
    }
    console.log(
      'AUTO_HWM_DETAIL'
      + ',pattern=SPOT'
      + `,transport=${transport}`
      + `,component=${component}`
      + `,msg_size=${msgSize}`
      + `,owner=${spotSocketOwnerName(entry.owner)}`
      + `,owner_id=${entry.ownerId}`
      + `,socket=${entry.socketName}`
      + `,socket_type=${spotSocketTypeName(entry.socketType)}`
      + `,role=${autoHwmRoleName(snapshot.autoHwmRole)}`
      + `,sndhwm=${snapshot.autoHwmAppliedSndHwm}`
      + `,rcvhwm=${snapshot.autoHwmAppliedRcvHwm}`
      + `,effective_message_bytes=${snapshot.autoHwmEffectiveMessageBytes}`
      + `,effective_sndbuf=${snapshot.autoHwmEffectiveSndBuf}`
      + `,effective_rcvbuf=${snapshot.autoHwmEffectiveRcvBuf}`
      + `,socket_message_slots=${snapshot.autoHwmSocketMessageSlots}`
    );
  }
}

function applyContextPolicy(ctx) {
  const ioThreads = integerEnv('PERF_IO_THREADS', 0);
  if (ioThreads > 0) {
    ctx.options.ioThreads = ioThreads;
  }
  const maxSockets = integerEnv('PERF_MAX_SOCKETS', NaN);
  if (Number.isFinite(maxSockets) && maxSockets > 0) {
    ctx.options.maxSockets = maxSockets;
  }
  if ('autoHwmEnabled' in ctx.options) {
    ctx.options.autoHwmEnabled = integerEnv('PERF_CTX_AUTO_HWM_ENABLE', 1) !== 0;
  }
  if ('autoHwmProfile' in ctx.options && zlink.AutoHwmProfile) {
    const profile = String(process.env.PERF_CTX_AUTO_HWM_PROFILE || '').trim();
    if (profile === 'compact') {
      ctx.options.autoHwmProfile = zlink.AutoHwmProfile.Compact;
    } else if (profile === 'low_latency' || profile === 'low-latency') {
      ctx.options.autoHwmProfile = zlink.AutoHwmProfile.LowLatency;
    } else if (profile === 'throughput') {
      ctx.options.autoHwmProfile = zlink.AutoHwmProfile.Throughput;
    } else {
      ctx.options.autoHwmProfile = zlink.AutoHwmProfile.Balanced;
    }
  }
}

function resolveSingleLatencySampleCap() {
  const configured = integerEnv('PERF_SINGLE_LATENCY_SAMPLE_CAP', 200000);
  return configured > 0 ? configured : 200000;
}

function recvNoWait(socket) {
  const received = new zlink.Received();
  try {
    return socket.recv(received, RecvFlags.DontWait) ? received : null;
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

function subscribeNoWait(socket) {
  try {
    return socket.subscribe(RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === RecvResult.NoData) {
      return null;
    }
    throw error;
  }
}

async function waitForConnectionReady(
  socket,
  connectFn = null,
  timeoutMs = integerEnv('PERF_CONNECT_READY_TIMEOUT_MS', 5000)
) {
  const monitor = socket.monitorOpen([MonitorEventType.ConnectionReady]);
  try {
    if (typeof connectFn === 'function') {
      await connectFn();
    }
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      try {
        const event = monitor.recv(RecvFlags.DontWait);
        if (event && event.event === MonitorEventType.ConnectionReady) {
          return;
        }
      } catch (error) {
        if (!(error instanceof zlink.RecvError && error.result === RecvResult.NoData)) {
          throw error;
        }
      }
      await sleepImmediate();
    }
    throw new Error(`connection ready timeout after ${timeoutMs}ms`);
  } finally {
    monitor.close();
  }
}

async function waitForMonitorConnectionReady(
  monitor,
  timeoutMs = integerEnv('PERF_CONNECT_READY_TIMEOUT_MS', 5000)
) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const event = monitor.recv(RecvFlags.DontWait);
      if (event && event.event === MonitorEventType.ConnectionReady) {
        return;
      }
    } catch (error) {
      if (!(error instanceof zlink.RecvError && error.result === RecvResult.NoData)) {
        throw error;
      }
    }
    await sleepImmediate();
  }
  throw new Error(`connection ready timeout after ${timeoutMs}ms`);
}

async function waitForPostReadySettle(timeoutMs) {
  const deadline = Date.now() + Math.max(0, timeoutMs | 0);
  while (Date.now() < deadline) {
    await sleepImmediate();
  }
}

// PERF_SINGLE_TEST_POLICY § 1.4: receiver waits with `-1` (signal-driven)
// and exits on the wire-level stop token. The legacy `shouldStop()` flag
// (and short `pollTimeoutMs`) are no longer used; phase end is signaled
// purely by the sender emitting `STOP_TOKEN_BYTES` on the wire.
//
// Note: Node's `pollerWaitMany` is a synchronous N-API call that blocks the
// JS event loop until a wakeup arrives. Callers spawn this drain as an
// unawaited Promise, so we must yield to the event loop at least once
// before the first blocking wait — otherwise queued worker postMessages
// (e.g. the `start` command) cannot be delivered and the worker never
// produces data, causing a deadlock.
async function drainRecvSocket(socket, onMessage) {
  const poller = new zlink.Poller();
  poller.add(socket, pollEvents(POLLIN));
  const useSubscribe = typeof socket.subscribe === 'function';

  try {
    let stopReceived = false;
    let iterCount = 0;
    let totalReceived = 0;
    if (process.env.PERF_NODE_TRACE === '1') {
      console.error(`[drainRecvSocket] entry`);
    }
    while (!stopReceived) {
      iterCount += 1;
      if (process.env.PERF_NODE_TRACE === '1' && (iterCount === 1 || iterCount % 100 === 0)) {
        console.error(`[drainRecvSocket] iter=${iterCount} totalReceived=${totalReceived}`);
      }
      await sleepImmediate();
      let ready = [];
      try {
        ready = poller.waitMany(Math.max(1, poller.size), -1);
      } catch (error) {
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
          continue;
        }
        throw error;
      }
      if (ready.length === 0) {
        // Spurious wake-ups are unexpected with `-1`, but treat them as
        // benign and keep waiting.
        continue;
      }
      while (true) {
        const received = useSubscribe ? subscribeNoWait(socket) : recvNoWait(socket);
        if (!received) {
          break;
        }
        if (isStopTokenParts(received.parts)) {
          stopReceived = true;
          break;
        }
        onMessage(received);
      }
    }
  } finally {
    poller.close();
  }
}

function isTransientSubmit(error) {
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

function sendSocketNoWait(socket, payload, flags = zlink.SendFlags.DontWait) {
  try {
    return socket.send().message(payload).flags(flags).submit();
  } catch (error) {
    if (isTransientSubmit(error)) {
      return false;
    }
    throw error;
  }
}

function drainRecvSocketNoWaitUntilIdle(socket, collector) {
  let stopReceived = false;
  while (true) {
    const received = recvNoWait(socket);
    if (!received) {
      return stopReceived;
    }
    if (isStopTokenParts(received.parts)) {
      stopReceived = true;
      continue;
    }
    const header = decodeMetricHeaderFromParts(received.parts);
    collector.record(header, currentEpochNs());
  }
}

async function runLocalSocketOneWayBenchmark({
  pattern,
  msgSize,
  options,
  endpointToken,
  createReceiver,
  createSender,
  configureReceiver = null,
  configureSender = null
}) {
  const ctx = new zlink.Context();
  applyContextPolicy(ctx);
  const receiver = createReceiver(ctx);
  const sender = createSender(ctx);
  const receiverMonitor = receiver.monitorOpen([MonitorEventType.ConnectionReady]);
  const senderMonitor = sender.monitorOpen([MonitorEventType.ConnectionReady]);
  const endpoint = await benchmarkEndpoint(options.transport, `${endpointToken}-${msgSize}`);

  try {
    applySocketPolicy(receiver, options);
    applySocketPolicy(sender, options);
    applyAutoHwmMsgUnit(receiver, msgSize);
    applyAutoHwmMsgUnit(sender, msgSize);
    if (typeof configureReceiver === 'function') {
      configureReceiver(receiver);
    }
    if (typeof configureSender === 'function') {
      configureSender(sender);
    }
    ctx.recalculateAutoHwm();
    receiver.bind(endpoint);
    sender.connect(endpoint);
    await waitForMonitorConnectionReady(receiverMonitor);
    await waitForMonitorConnectionReady(senderMonitor);

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
      if (sendSocketNoWait(sender, payload)) {
        seq += 1n;
      }
      drainRecvSocketNoWaitUntilIdle(receiver, collector);
    }
    stampPayload(payload, { phase: 2, runId, msgSize, seq });
    sendSocketNoWait(sender, payload);
    for (let retry = 0; retry < 100; retry += 1) {
      if (sendSocketNoWait(sender, STOP_TOKEN_BYTES, zlink.SendFlags.None)) {
        break;
      }
    }
    while (!drainRecvSocketNoWaitUntilIdle(receiver, collector)) {
      // Drain until the wire-level stop token arrives.
    }
    const result = collector.finish();
    emitSingleSocketHwmDetail(receiver, pattern, options.transport, 'receiver', msgSize);
    emitSingleSocketHwmDetail(sender, pattern, options.transport, 'sender', msgSize);
    return result;
  } finally {
    receiverMonitor.close();
    senderMonitor.close();
    receiver.close();
    sender.close();
    ctx.close();
  }
}

function parseSingleBinaryArgs(argv) {
  if (argv.length < 3) {
    throw new Error('usage: <binary> <lib_name> <transport> <size>');
  }
  const transport = String(argv[1] || '').trim().toLowerCase();
  const msgSize = Number(argv[2]);
  if (!Number.isFinite(msgSize) || msgSize < MIN_MSG_SIZE) {
    throw new Error(`invalid single msg size: ${argv[2]}`);
  }
  return {
    libName: String(argv[0] || 'current'),
    transport,
    msgSize,
    duration: integerEnv('PERF_SINGLE_DURATION_SECONDS', 5),
    runId: 1,
    hwm: integerEnv('PERF_SINGLE_HWM', NaN),
    sendHwm: integerEnv('PERF_SINGLE_SNDHWM', NaN),
    recvHwm: integerEnv('PERF_SINGLE_RCVHWM', NaN),
    sendTimeoutMs: integerEnv('PERF_SINGLE_SNDTIMEO_MS', NaN),
    recvTimeoutMs: integerEnv('PERF_SINGLE_RCVTIMEO_MS', NaN)
  };
}

function spawnSenderWorker(workerData) {
  const worker = new Worker(
    path.join(__dirname, 'perf_single_sender_worker.js'),
    { workerData }
  );
  worker.__seenMessages = [];
  worker.__waiters = [];
  worker.on('message', (message) => {
    worker.__seenMessages.push(message);
    for (const waiter of worker.__waiters.slice()) {
      if (waiter(message)) {
        return;
      }
    }
  });
  return worker;
}

function waitForWorkerMessage(worker, expectedType, timeoutMs = integerEnv('PERF_CONNECT_READY_TIMEOUT_MS', 5000)) {
  return new Promise((resolve, reject) => {
    const seen = worker.__seenMessages.find((message) => message && message.type === expectedType);
    if (seen) {
      resolve(seen);
      return;
    }

    let done = false;
    const timeout = setTimeout(() => {
      if (done) {
        return;
      }
      done = true;
      reject(new Error(`worker timeout waiting for ${expectedType}`));
    }, timeoutMs);

    const onExit = (code) => {
      if (done) {
        return;
      }
      done = true;
      clearTimeout(timeout);
      reject(new Error(`worker exited before ${expectedType}: ${code}`));
    };
    worker.once('exit', onExit);

    worker.__waiters.push((message) => {
      if (done || !message || message.type !== expectedType) {
        return false;
      }
      done = true;
      clearTimeout(timeout);
      worker.off('exit', onExit);
      resolve(message);
      return true;
    });
  });
}

function waitForWorkerError(worker) {
  return new Promise((resolve) => {
    const seen = worker.__seenMessages.find((message) => message && message.type === 'error');
    if (seen) {
      resolve(seen);
      return;
    }

    worker.__waiters.push((message) => {
      if (!message || message.type !== 'error') {
        return false;
      }
      resolve(message);
      return true;
    });
  });
}

function waitForWorkerDone(worker, durationSeconds) {
  const readyTimeoutMs = integerEnv('PERF_CONNECT_READY_TIMEOUT_MS', 5000);
  const activeMs = Math.ceil(Math.max(0, Number(durationSeconds) || 0) * 1000);
  return waitForWorkerMessage(worker, 'done', activeMs + readyTimeoutMs);
}

async function closeSenderWorker(worker) {
  if (!worker) {
    return;
  }
  const waitForExit = new Promise((resolve) => {
    worker.once('exit', () => resolve());
  });
  try {
    worker.postMessage({ type: 'stop' });
  } catch (err) {
    console.error(`[perf] close failed: ${err}`);
  }
  const exited = await Promise.race([
    waitForExit.then(() => true),
    new Promise((resolve) => setTimeout(() => resolve(false), 1000))
  ]);
  if (!exited && worker.threadId && Number.isFinite(worker.threadId)) {
    try {
      await worker.terminate();
    } catch (err) {
      console.error(`[perf] close failed: ${err}`);
    }
  }
}

module.exports = {
  applyAutoHwmMsgUnit,
  applyContextPolicy,
  applySpotNodeAdmission,
  applySocketPolicy,
  configureTlsClient,
  configureTlsServer,
  emitSingleSocketHwmDetail,
  emitSingleSpotHwmDetail,
  benchmarkEndpoint,
  closeSenderWorker,
  drainRecvSocket,
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  runLocalSocketOneWayBenchmark,
  spawnSenderWorker,
  waitForWorkerDone,
  waitForWorkerError,
  waitForWorkerMessage,
  waitForPostReadySettle,
  waitForConnectionReady,
  waitForMonitorConnectionReady,
};
