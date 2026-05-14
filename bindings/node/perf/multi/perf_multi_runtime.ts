// SPDX-License-Identifier: MPL-2.0

'use strict';

const zlink = require('@zlink-systems/zlink');
const readline = require('node:readline');
const { MonitorEventType, RecvFlags, RecvResult } = zlink;
const {
  applyAutoHwmMsgUnit,
  applyAutoHwmProfile,
  integerEnv,
  manualSocketOverridesEnabled,
  sleepImmediate
} = require('../common/perf_metrics');
const POLLIN = 1;
const POLLOUT = 2;
const emittedMultiAutoHwmDetails = new Set();

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

function applySocketPolicy(socket, options = {}) {
  const sendTimeout = integerEnv('PERF_MULTI_SNDTIMEO_MS', 200);
  const recvTimeout = integerEnv('PERF_MULTI_RCVTIMEO_MS', 200);
  const linger = integerEnv('PERF_MULTI_LINGER_MS', 0);

  if (socket.options) {
    if (manualSocketOverridesEnabled('multi')) {
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
  if (!manualSocketOverridesEnabled('multi')) {
    return;
  }
  const hwm = integerEnv('PERF_MULTI_HWM', 1000);
  const sendHwm = integerEnv('PERF_MULTI_SNDHWM', hwm);
  const recvHwm = integerEnv('PERF_MULTI_RCVHWM', hwm);
  node.pubsubHwm = sendHwm;
  node.routerHwm = recvHwm;
}

function autoHwmDetailEnabled() {
  const value = process.env.PERF_MULTI_PRINT_AUTO_HWM_DETAIL
    ?? process.env.PERF_PRINT_AUTO_HWM_DETAIL;
  return value === undefined || value === '' || value !== '0';
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

function autoHwmProfileName(profile) {
  if (zlink.AutoHwmProfile) {
    if (profile === zlink.AutoHwmProfile.Compact) return 'compact';
    if (profile === zlink.AutoHwmProfile.LowLatency) return 'low_latency';
    if (profile === zlink.AutoHwmProfile.Balanced) return 'balanced';
    if (profile === zlink.AutoHwmProfile.Throughput) return 'throughput';
  }
  return 'unknown';
}

function autoHwmPolicyClassName(policyClass) {
  switch (policyClass) {
    case 1: return 'fanout';
    case 2: return 'spot_data';
    case 3: return 'recv_ingress';
    case 4: return 'routed';
    case 5: return 'control';
    case 6: return 'stream';
    case 7: return 'peer_queue';
    default: return 'unknown';
  }
}

function autoHwmRecalcReasonName(reason) {
  switch (reason) {
    case 1: return 'context_create';
    case 2: return 'socket_register';
    case 3: return 'socket_unregister';
    case 4: return 'socket_option';
    case 5: return 'manual';
    case 6: return 'budget_change';
    case 7: return 'topology_change';
    case 8: return 'timer';
    default: return 'unknown';
  }
}

function socketTypeName(socketOrType) {
  const socketType = typeof socketOrType === 'number' ? socketOrType : null;
  if (socketType !== null && zlink.SocketType) {
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
  const socket = socketOrType;
  if (socket instanceof zlink.PairSocket) return 'pair';
  if (socket instanceof zlink.PubSocket) return 'pub';
  if (socket instanceof zlink.SubSocket) return 'sub';
  if (socket instanceof zlink.DealerSocket) return 'dealer';
  if (socket instanceof zlink.RouterSocket) return 'router';
  if (zlink.StreamSocket && socket instanceof zlink.StreamSocket) return 'stream';
  return 'unknown';
}

function autoHwmSendSideVisible(socketType, role) {
  const roleName = autoHwmRoleName(role);
  return roleName === 'fanout'
    || roleName === 'spot_data'
    || roleName === 'routed'
    || roleName === 'control'
    || roleName === 'peer_queue'
    || roleName === 'stream'
    || socketTypeName(socketType) === 'pub'
    || socketTypeName(socketType) === 'router'
    || socketTypeName(socketType) === 'dealer'
    || socketTypeName(socketType) === 'stream';
}

function autoHwmRecvSideVisible(socketType, role) {
  const roleName = autoHwmRoleName(role);
  return roleName === 'recv_ingress'
    || roleName === 'routed'
    || roleName === 'control'
    || roleName === 'peer_queue'
    || roleName === 'stream'
    || socketTypeName(socketType) === 'sub'
    || socketTypeName(socketType) === 'router'
    || socketTypeName(socketType) === 'dealer'
    || socketTypeName(socketType) === 'stream';
}

function hwmSndBufDisplay(snapshot, socketType) {
  return autoHwmSendSideVisible(socketType, snapshot.autoHwmRole)
    ? String(snapshot.autoHwmEffectiveSndBuf)
    : '-';
}

function hwmRcvBufDisplay(snapshot, socketType) {
  return autoHwmRecvSideVisible(socketType, snapshot.autoHwmRole)
    ? String(snapshot.autoHwmEffectiveRcvBuf)
    : '-';
}

function emitMultiSocketHwmDetail(socket, label, transport, msgSize) {
  if (!socket || !autoHwmDetailEnabled()) {
    return;
  }
  let monitor = null;
  try {
    monitor = socket.monitorOpen([MonitorEventType.ConnectionReady]);
    const snapshot = monitor.snapshot();
    const pattern = process.env.PERF_MULTI_PATTERN || process.env.PERF_PATTERN || 'unknown';
    const component = process.env.PERF_MULTI_COMPONENT || 'process';
    const effectiveTransport = transport || process.env.PERF_MULTI_TRANSPORT || 'unknown';
    const socketType = socketTypeName(socket);
    const key = [
      pattern,
      effectiveTransport,
      component,
      label || 'socket',
      msgSize || 0,
      autoHwmRoleName(snapshot.autoHwmRole),
      snapshot.autoHwmAppliedSndHwm,
      snapshot.autoHwmAppliedRcvHwm,
      snapshot.autoHwmProfile,
      snapshot.autoHwmPolicyClass,
      String(snapshot.autoHwmUnitBudgetBytes),
      snapshot.autoHwmSizeCap,
      String(snapshot.autoHwmSocketMessageSlots),
      String(snapshot.autoHwmEffectiveMessageBytes),
      snapshot.autoHwmEffectiveSndBuf,
      snapshot.autoHwmEffectiveRcvBuf,
    ].join('|');
    if (emittedMultiAutoHwmDetails.has(key)) {
      return;
    }
    emittedMultiAutoHwmDetails.add(key);
    console.log(
      'AUTO_HWM_DETAIL'
      + `,pattern=${pattern}`
      + `,transport=${effectiveTransport}`
      + `,component=${component}`
      + `,label=${label || 'socket'}`
      + `,socket_type=${socketType}`
      + `,msg_size=${msgSize || 0}`
      + ',source=monitor_snapshot'
      + `,enabled=${snapshot.autoHwmEnabled ? 1 : 0}`
      + `,role=${autoHwmRoleName(snapshot.autoHwmRole)}`
      + `,role_id=${snapshot.autoHwmRole}`
      + `,profile=${autoHwmProfileName(snapshot.autoHwmProfile)}`
      + `,profile_id=${snapshot.autoHwmProfile}`
      + `,policy_class=${autoHwmPolicyClassName(snapshot.autoHwmPolicyClass)}`
      + `,policy_class_id=${snapshot.autoHwmPolicyClass}`
      + `,unit_budget_bytes=${snapshot.autoHwmUnitBudgetBytes}`
      + `,size_cap=${snapshot.autoHwmSizeCap}`
      + `,sndhwm=${snapshot.autoHwmAppliedSndHwm}`
      + `,rcvhwm=${snapshot.autoHwmAppliedRcvHwm}`
      + `,socket_message_slots=${snapshot.autoHwmSocketMessageSlots}`
      + `,effective_message_bytes=${snapshot.autoHwmEffectiveMessageBytes}`
      + `,effective_sndbuf=${hwmSndBufDisplay(snapshot, socket)}`
      + `,effective_rcvbuf=${hwmRcvBufDisplay(snapshot, socket)}`
      + `,last_recalc_ms=${snapshot.autoHwmLastRecalcMs}`
      + `,last_recalc_reason=${autoHwmRecalcReasonName(snapshot.autoHwmLastRecalcReason)}`
      + `,send_blocked_ratio_ppm=${snapshot.autoHwmSendBlockedRatioPpm}`
      + `,deferred_sndhwm=${snapshot.autoHwmDeferredSndHwm}`
      + `,deferred_rcvhwm=${snapshot.autoHwmDeferredRcvHwm}`
    );
  } catch (err) {
    // Diagnostic output must not turn a valid perf run into a failure.
  } finally {
    monitor?.close();
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

  return 4;
}

function applyContextPolicy(ctx, role, pattern) {
  ctx.options.ioThreads = resolveMultiIoThreads(role, pattern);
  const maxSockets = integerEnv('PERF_MAX_SOCKETS', NaN);
  if (Number.isFinite(maxSockets) && maxSockets > 0) {
    ctx.options.maxSockets = maxSockets;
  }
  ctx.options.blocky = integerEnv('PERF_CTX_BLOCKY', 0) !== 0;
  ctx.options.autoHwmEnabled = true;
  applyAutoHwmProfile(ctx, zlink);
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
// router.send(routingId, ...)); a failed sentinel is a benchmark failure.
async function sendStopTokenOnce(_socket, sendFn) {
  const stopBytes = require('../perf_stop_token').STOP_TOKEN_BYTES;
  if (!sendFn(stopBytes)) {
    throw new Error('stop token send failed');
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

async function publishControlUntilSent(socket, waiter, topic, payload) {
  const body = Buffer.isBuffer(payload) ? payload : Buffer.from(String(payload));
  for (;;) {
    if (trySocketPublish(socket, topic, body)) {
      return;
    }
    await waiter.wait(POLLOUT);
  }
}

async function waitForRunnerControlConnected() {
  const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  try {
    for await (const line of rl) {
      if (line.startsWith('CONTROL_CONNECTED,')) {
        return;
      }
      if (line === 'STOP' || line === 'QUIT') {
        return;
      }
    }
  } finally {
    rl.close();
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
  emitMultiSocketHwmDetail,
  pollEvents,
  pollEventHas,
  publishControlUntilSent,
  recvNoWait,
  recvNoWaitInto,
  sendStopTokenOnce,
  subscribeNoWait,
  trySocketPublish,
  trySocketSend,
  waitForRunnerControlConnected,
  waitForConnectionReadyCount,
  waitForConnectionReady
};
