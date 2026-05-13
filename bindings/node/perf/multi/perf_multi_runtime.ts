// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('@zlink-systems/zlink');
const { MonitorEventType, RecvFlags, RecvResult } = zlink;
const { sleepImmediate } = require('../common/perf_metrics');
const { isStopTokenParts } = require('../perf_stop_token');
const POLLIN = 1;
const POLLOUT = 2;

function pollEvents(mask) {
  const events = [];
  if ((mask & POLLIN) !== 0) {
    events.push(zlink.PollEventFlag.PollIn);
  }
  if ((mask & POLLOUT) !== 0) {
    events.push(zlink.PollEventFlag.PollOut);
  }
  return events;
}

function pollEventHas(event, mask) {
  if (Array.isArray(event?.revents)) {
    return event.revents.some((flag) => (flag & mask) !== 0);
  }
  return ((event?.events ?? 0) & mask) !== 0;
}

function integerEnv(name, fallback) {
  const raw = process.env[name];
  if (raw === undefined || raw === '') {
    return fallback;
  }
  const parsed = Number(raw);
  return Number.isFinite(parsed) ? Math.trunc(parsed) : fallback;
}

function manualSocketOverridesEnabled() {
  return (
    integerEnv('PERF_MULTI_ALLOW_MANUAL_SOCKET_OVERRIDES', 0) > 0 ||
    integerEnv('PERF_ALLOW_MANUAL_SOCKET_OVERRIDES', 0) > 0
  );
}

// PERF_MULTI_TEST_POLICY § 1.3.1: client/server poller waits use `-1`
// (signal-driven). Phase end is signaled on the wire via the stop token,
// so the legacy timer-based fallback (default 100 ms) is no longer
// needed. Negative values pass through directly so callers can opt into
// the indefinite wait without sentinel handling.
function resolveClientPollTimeoutMs() {
  const raw = process.env.PERF_CLIENT_POLL_TIMEOUT_MS;
  if (raw === undefined || raw === '') {
    return -1;
  }
  const parsed = Number(raw);
  if (!Number.isFinite(parsed)) {
    return -1;
  }
  return Math.trunc(parsed);
}

function applySocketPolicy(socket, options = {}) {
  const sendTimeout = integerEnv('PERF_MULTI_SNDTIMEO_MS', 200);
  const recvTimeout = integerEnv('PERF_MULTI_RCVTIMEO_MS', 200);
  const linger = integerEnv('PERF_MULTI_LINGER_MS', 0);

  if (socket.options) {
    if (manualSocketOverridesEnabled()) {
      const hwm = integerEnv('PERF_MULTI_HWM', 1000);
      const sendHwm = integerEnv('PERF_MULTI_SNDHWM', hwm);
      const recvHwm = integerEnv('PERF_MULTI_RCVHWM', hwm);
      socket.options.sendHwm = sendHwm;
      socket.options.recvHwm = recvHwm;
    }
    socket.options.sendTimeout = sendTimeout;
    socket.options.recvTimeout = options.recvTimeout ?? recvTimeout;
    socket.options.linger = linger;
    if ('noDrop' in socket.options && options.noDrop !== undefined) {
      socket.options.noDrop = Boolean(options.noDrop);
    }
  }
}

function applySpotNodeAdmission(node) {
  if (!manualSocketOverridesEnabled()) {
    return;
  }
  const hwm = integerEnv('PERF_MULTI_HWM', 1000);
  const sendHwm = integerEnv('PERF_MULTI_SNDHWM', hwm);
  const recvHwm = integerEnv('PERF_MULTI_RCVHWM', hwm);
  node.pubsubHwm = sendHwm;
  node.routerHwm = recvHwm;
}

function applyAutoHwmMsgUnit(socket, msgSize) {
  if (msgSize <= 0 || !socket.options) {
    return;
  }
  try {
    socket.options.autoHwmMsgUnitBytes = msgSize;
  } catch (err) {
    // best effort — not all socket types expose this option
  }
}

function resolveMultiIoThreads(role, pattern) {
  const normalizedRole = String(role || '').trim().toLowerCase();
  const roleKey = normalizedRole === 'server' ? 'SERVER' : 'CLIENT';
  const isStream = pattern === 'MULTI_STREAM';
  const envNames = isStream
    ? [`PERF_MULTI_STREAM_${roleKey}_IO_THREADS`, `PERF_MULTI_${roleKey}_IO_THREADS`]
    : [`PERF_MULTI_${roleKey}_IO_THREADS`];

  for (const name of envNames) {
    const value = integerEnv(name, NaN);
    if (Number.isFinite(value) && value >= 0) {
      return value;
    }
  }

  const shared = integerEnv('PERF_IO_THREADS', NaN);
  if (Number.isFinite(shared) && shared >= 0) {
    return shared;
  }

  const fallback = integerEnv('PERF_MULTI_DEFAULT_IO_THREADS', NaN);
  if (Number.isFinite(fallback) && fallback >= 0) {
    return fallback;
  }

  return 8;
}

function resolveAutoHwmProfile() {
  const env = process.env.PERF_CTX_AUTO_HWM_PROFILE || process.env.PERF_AUTO_HWM_PROFILE || '';
  if (env === 'compact') {
    return zlink.AutoHwmProfile.Compact;
  }
  if (env === 'low_latency' || env === 'low-latency') {
    return zlink.AutoHwmProfile.LowLatency;
  }
  if (env === 'throughput') {
    return zlink.AutoHwmProfile.Throughput;
  }
  return zlink.AutoHwmProfile.Balanced;
}

function applyContextPolicy(ctx, role, pattern) {
  ctx.options.ioThreads = resolveMultiIoThreads(role, pattern);
  const maxSockets = integerEnv('PERF_MAX_SOCKETS', NaN);
  if (Number.isFinite(maxSockets) && maxSockets > 0) {
    ctx.options.maxSockets = maxSockets;
  }
  ctx.options.blocky = integerEnv('PERF_CTX_BLOCKY', 0) !== 0;
  ctx.options.autoHwmEnabled = true;
  ctx.options.autoHwmProfile = resolveAutoHwmProfile();
}

function resolveMultiLatencySampleCap() {
  const configured = integerEnv('PERF_MULTI_LATENCY_SAMPLE_CAP', 200000);
  return configured > 0 ? configured : 200000;
}

function recvNoWait(socket) {
  const received = new zlink.Received();
  return recvNoWaitInto(socket, received) ? received : null;
}

function recvNoWaitInto(socket, received) {
  try {
    return socket.recv(received, RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === RecvResult.NoData) {
      return false;
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
  timeoutMs = integerEnv('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 5000)
) {
  return waitForConnectionReadyCount(socket, 1, connectFn, timeoutMs);
}

async function waitForConnectionReadyCount(
  socket,
  expectedCount,
  connectFn = null,
  timeoutMs = integerEnv('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 5000)
) {
  const monitor = socket.monitorOpen([MonitorEventType.ConnectionReady]);
  try {
    if (typeof connectFn === 'function') {
      await connectFn();
    }
    const targetCount = Math.max(1, Math.trunc(expectedCount || 1));
    let readyCount = 0;
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
      let drained = false;
      try {
        while (true) {
          const event = monitor.recv(RecvFlags.DontWait);
          if (!event) {
            break;
          }
          drained = true;
          if (event.event === MonitorEventType.ConnectionReady) {
            readyCount += 1;
            if (readyCount >= targetCount) {
              return;
            }
          }
        }
      } catch (error) {
        if (!(error instanceof zlink.RecvError && error.result === RecvResult.NoData)) {
          throw error;
        }
      }
      if (!drained) {
        await sleepImmediate();
      }
    }
    throw new Error(
      `connection ready timeout after ${timeoutMs}ms (${readyCount}/${targetCount})`
    );
  } finally {
    monitor.close();
  }
}

function trySocketSend(socket, ...args) {
  try {
    const routed = args.length >= 2 && args[0] instanceof zlink.RoutingId;
    const payload = routed ? args[1] : args[0];
    let op = routed ? socket.send(args[0]) : socket.send();
    const parts = Array.isArray(payload) ? payload : [payload];
    for (const part of parts) {
      op = op.message(part);
    }
    return op.flags(zlink.SendFlags.DontWait).submit();
  } catch (error) {
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    const text = String(error && error.message ? error.message : error);
    if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
      return false;
    }
    throw error;
  }
}

// PERF_MULTI_TEST_POLICY § 1.3.1: emit the wire-level stop token once at
// phase end. Callers pass a closure that performs the actual send (e.g.
// router.send(routingId, ...)). The active loop has already exited and
// the socket's existing poller is no longer being driven, so we yield to
// the event loop on transient backpressure rather than registering the
// same socket with a second poller.
async function sendStopTokenWithRetry(_socket, sendFn) {
  const stopBytes = require('../perf_stop_token').STOP_TOKEN_BYTES;
  for (let retry = 0; retry < 100; retry += 1) {
    if (sendFn(stopBytes)) {
      return;
    }
    await sleepImmediate();
  }
}

function trySocketPublish(socket, topic, payload) {
  try {
    let op = socket.publish(topic);
    const parts = Array.isArray(payload) ? payload : [payload];
    for (const part of parts) {
      op = op.message(part);
    }
    return op.flags(zlink.SendFlags.DontWait).submit();
  } catch (error) {
    if (error instanceof zlink.SubmitError && error.result === zlink.SubmitResult.Backpressured) {
      return false;
    }
    const text = String(error && error.message ? error.message : error);
    if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
      return false;
    }
    throw error;
  }
}

// PERF_MULTI_TEST_POLICY § 1.3.1: receivers wait with `-1` (signal-driven)
// and exit on the wire-level stop token. The legacy `shouldStop()` flag is
// kept as an optional belt-and-suspenders so callers can still bail out
// (e.g. when a connection close races the sentinel), but it is no longer
// the primary termination signal.
async function drainRecvSocket(socket, onMessage, shouldStop) {
  const poller = new zlink.Poller();
  poller.add(socket, pollEvents(POLLIN));
  const useSubscribe = typeof socket.subscribe === 'function';
  const checkStop = typeof shouldStop === 'function' ? shouldStop : () => false;

  try {
    let stopReceived = false;
    while (!stopReceived && !checkStop()) {
      // Yield to the event loop before each blocking wait so queued
      // postMessages, timers, and stdin events are delivered. The native
      // `pollerWait(-1)` is synchronous and would otherwise starve the
      // rest of the JS world.
      await sleepImmediate();
      let ready = null;
      try {
        ready = poller.wait(-1);
      } catch (error) {
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
          continue;
        }
        throw error;
      }
      if (!ready) {
        // `-1` should not normally produce a null event; treat as spurious
        // wake-up and re-arm.
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

function createSocketEventWaiter(socket, events) {
  const poller = new zlink.Poller();
  poller.add(socket, pollEvents(events));

  return {
    // PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven `-1` wait. The core
    // emits a wakeup on every relevant event, so timer fallbacks are not
    // needed. The leading `sleepImmediate()` lets queued microtasks run
    // before we descend into the synchronous N-API wait.
    async wait(mask = events) {
      while (true) {
        await sleepImmediate();
        let ready = null;
        try {
          ready = poller.wait(-1);
        } catch (error) {
          const text = String(error && error.message ? error.message : error);
          if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
            continue;
          }
          throw error;
        }
        if (ready && pollEventHas(ready, mask)) {
          return ready;
        }
      }
    },
    close() {
      poller.close();
    }
  };
}

module.exports = {
  POLLIN,
  POLLOUT,
  applyContextPolicy,
  applyAutoHwmMsgUnit,
  applySpotNodeAdmission,
  applySocketPolicy,
  createSocketEventWaiter,
  drainRecvSocket,
  manualSocketOverridesEnabled,
  pollEvents,
  pollEventHas,
  recvNoWait,
  recvNoWaitInto,
  resolveClientPollTimeoutMs,
  resolveMultiLatencySampleCap,
  sendStopTokenWithRetry,
  subscribeNoWait,
  trySocketPublish,
  trySocketSend,
  waitForConnectionReadyCount,
  waitForConnectionReady
};
