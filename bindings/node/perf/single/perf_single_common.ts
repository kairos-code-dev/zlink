// SPDX-License-Identifier: MPL-2.0

'use strict';

const net = require('node:net');
const os = require('node:os');
const path = require('node:path');
const { once } = require('node:events');
const { Worker } = require('node:worker_threads');
const zlink = require('../../..');
const {
  MonitorEventType,
  RecvFlags,
  RecvResult
} = zlink;
const {
  MIN_MSG_SIZE,
  integerEnv,
  sleepImmediate
} = require('../common/perf_metrics');
const {
  configureTlsClient,
  configureTlsServer,
} = require('../common/perf_tls');
const { isStopTokenParts } = require('../perf_stop_token');
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
  const hwm = Number.isFinite(options.hwm)
    ? options.hwm
    : integerEnv('PERF_SINGLE_HWM', 1000);
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
    socket.options.sendHwm = sendHwm;
    socket.options.recvHwm = recvHwm;
    socket.options.sendTimeout = sendTimeout;
    socket.options.recvTimeout = options.recvTimeout ?? recvTimeout;
    socket.options.linger = linger;
    if ('noDrop' in socket.options && options.noDrop !== undefined) {
      socket.options.noDrop = Boolean(options.noDrop);
    }
  }
}

function applySpotNodeAdmission(node, options = {}) {
  const hwm = Number.isFinite(options.hwm)
    ? options.hwm
    : integerEnv('PERF_SINGLE_HWM', 1000);
  const sendHwm = Number.isFinite(options.sendHwm)
    ? options.sendHwm
    : integerEnv('PERF_SINGLE_SNDHWM', hwm);
  const recvHwm = Number.isFinite(options.recvHwm)
    ? options.recvHwm
    : integerEnv('PERF_SINGLE_RCVHWM', hwm);

  node.pubsubHwm = sendHwm;
  node.routerHwm = recvHwm;
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

async function waitForSocketConnectionReady(
  socket,
  timeoutMs = integerEnv('PERF_CONNECT_READY_TIMEOUT_MS', 5000)
) {
  return waitForConnectionReady(socket, null, timeoutMs);
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

function resolveSingleIdleDrainMs(overrides = {}) {
  if (Number.isFinite(overrides.idleDrainMs)) {
    return Math.max(0, overrides.idleDrainMs);
  }
  if (Number.isFinite(overrides.recvTimeoutMs)) {
    return Math.max(0, overrides.recvTimeoutMs);
  }
  return integerEnv('PERF_SINGLE_RCVTIMEO_MS', 200);
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

function drainRecvNow(socket, onMessage) {
  if (typeof socket.subscribe === 'function') {
    while (true) {
      const received = subscribeNoWait(socket);
      if (!received) {
        return;
      }
      onMessage(received);
    }
    return;
  }
  while (true) {
    const received = recvNoWait(socket);
    if (!received) {
      break;
    }
    onMessage(received);
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
  applyContextPolicy,
  applySpotNodeAdmission,
  applySocketPolicy,
  configureTlsClient,
  configureTlsServer,
  benchmarkEndpoint,
  closeSenderWorker,
  drainRecvSocket,
  drainRecvNow,
  parseSingleBinaryArgs,
  resolveSingleLatencySampleCap,
  resolveSingleIdleDrainMs,
  spawnSenderWorker,
  waitForWorkerDone,
  waitForWorkerError,
  waitForWorkerMessage,
  waitForPostReadySettle,
  waitForConnectionReady,
  waitForSocketConnectionReady,
  waitForMonitorConnectionReady,
};
