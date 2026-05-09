// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('../../..');
const { MonitorEventType, RecvFlags, RecvResult } = zlink;
const { sleepImmediate } = require('../common/perf_metrics');
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

function resolveClientPollTimeoutMs() {
  const configured = integerEnv('PERF_CLIENT_POLL_TIMEOUT_MS', 100);
  return configured > 0 ? configured : 100;
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

  return isStream ? 4 : 2;
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
  try {
    return socket.recv(RecvFlags.DontWait);
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
    return socket.send(...args, zlink.SendFlags.DontWait);
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

function trySocketPublish(socket, topic, payload) {
  try {
    return socket.publish(topic, payload, zlink.SendFlags.DontWait);
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

async function drainRecvSocket(socket, onMessage, shouldStop) {
  const clientPollTimeoutMs = resolveClientPollTimeoutMs();
  const poller = new zlink.Poller();
  poller.add(socket, pollEvents(POLLIN));

  try {
    while (!shouldStop()) {
      let ready = null;
      try {
        ready = poller.wait(clientPollTimeoutMs);
      } catch (error) {
        const text = String(error && error.message ? error.message : error);
        if ((error && error.code === 'EAGAIN') || text.includes('Resource temporarily unavailable')) {
          continue;
        }
        throw error;
      }
      if (!ready) {
        continue;
      }
      if (typeof socket.subscribe === 'function') {
        while (true) {
          const received = subscribeNoWait(socket);
          if (!received) {
            break;
          }
          onMessage(received);
        }
      } else {
        while (true) {
          const received = recvNoWait(socket);
          if (!received) {
            break;
          }
          onMessage(received);
        }
      }
    }
  } finally {
    poller.close();
  }
}

function createSocketEventWaiter(socket, events) {
  const clientPollTimeoutMs = resolveClientPollTimeoutMs();
  const poller = new zlink.Poller();
  poller.add(socket, pollEvents(events));

  return {
    async wait(mask = events) {
      while (true) {
        let ready = null;
        try {
          ready = poller.wait(clientPollTimeoutMs);
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
  resolveClientPollTimeoutMs,
  resolveMultiLatencySampleCap,
  subscribeNoWait,
  trySocketPublish,
  trySocketSend,
  waitForConnectionReadyCount,
  waitForConnectionReady
};
