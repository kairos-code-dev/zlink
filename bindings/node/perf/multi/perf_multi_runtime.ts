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
const POLLCOMPLETION = 32;
const emittedMultiAutoHwmDetails = new Set();

function integerEnvPair(primary, fallbackName, fallback) {
  return integerEnv(primary, integerEnv(fallbackName, fallback));
}

function pollEvents(mask) {
  const events = [];
  if ((mask & POLLIN) !== 0) {
    events.push(zlink.PollEventFlag.PollIn);
  }
  if ((mask & POLLOUT) !== 0) {
    events.push(zlink.PollEventFlag.PollOut);
  }
  if ((mask & POLLCOMPLETION) !== 0) {
    events.push(zlink.PollEventFlag.PollCompletion);
  }
  return events;
}

function pollEventHas(event, mask) {
  return ((event?.revents ?? event?.events ?? 0) & mask) !== 0;
}

function waitPollerOne(poller, events, timeoutMs) {
  const count = poller.wait(events, timeoutMs);
  if (count <= 0) return null;
  return {
    sourceKind: events.sourceKind(0),
    slot: events.slot(0),
    revents: events.revents(0),
    fd: events.fd(0)
  };
}

function applySocketPolicy(socket, options = {}) {
  const linger = integerEnv('PERF_MULTI_LINGER_MS', 0);
  // C parity: bindings/c/perf/multi/common/perf_multi_runtime.hpp
  // apply_debug_timeouts (~986-997) sets ZLINK_OPT_SNDTIMEO/RCVTIMEO to
  // the 200ms default on every benchmark socket UNCONDITIONALLY, and
  // returns early (no timeouts) only for the inproc transport. The hot
  // path is still DONTWAIT send + `-1` poller wait; these socket timeouts
  // bound individual blocking calls exactly as in the C reference. Match
  // C: skip for inproc, otherwise apply the C default.
  const transport = String(
    options.transport || process.env.PERF_MULTI_TRANSPORT || ''
  ).trim().toLowerCase();
  const isInproc = transport === 'inproc';
  const sendTimeout = integerEnv('PERF_MULTI_SNDTIMEO_MS', 200);
  const recvTimeout = integerEnv('PERF_MULTI_RCVTIMEO_MS', 200);

  if (socket.options) {
    if (manualSocketOverridesEnabled('multi')) {
      const hwm = integerEnv('PERF_MULTI_HWM', 1000);
      const sendHwm = integerEnv('PERF_MULTI_SNDHWM', hwm);
      const recvHwm = integerEnv('PERF_MULTI_RCVHWM', hwm);
      socket.options.sendHwm = sendHwm;
      socket.options.recvHwm = recvHwm;
    }
    if (!isInproc) {
      socket.options.sendTimeout = sendTimeout;
      socket.options.recvTimeout = options.recvTimeout ?? recvTimeout;
    }
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

// C parity: perf_auto_hwm_sndbuf_display / perf_auto_hwm_rcvbuf_display
// (bindings/c/perf/multi/common/perf_multi_runtime.hpp ~L292-306). The
// AUTO_HWM_DETAIL `effective_sndbuf=`/`effective_rcvbuf=` tokens use the
// *narrow* visibility rule and emit "0" (not "-") for the hidden side, so
// the `## Auto-HWM Detail` collector renders the C `SNDBUF(KB)/RCVBUF(KB)`
// columns byte-identically (the emitter applies the "-" only to the
// SNDHWM/RCVHWM columns). The broad per-socket monitor rule must not be
// used here or the buffer columns diverge from C (notably MULTI_PUBSUB).
function hwmSndBufDisplay(snapshot, socket) {
  const typeName = socketTypeName(socket);
  const roleName = autoHwmRoleName(snapshot.autoHwmRole);
  return autoHwmSnapshotSendSideVisible(typeName, roleName)
    ? String(snapshot.autoHwmEffectiveSndBuf)
    : '0';
}

function hwmRcvBufDisplay(snapshot, socket) {
  const typeName = socketTypeName(socket);
  const roleName = autoHwmRoleName(snapshot.autoHwmRole);
  return autoHwmSnapshotRecvSideVisible(typeName, roleName)
    ? String(snapshot.autoHwmEffectiveRcvBuf)
    : '0';
}

// C parity: bindings/c/perf/multi/common/perf_multi_runtime.hpp
// perf_socket_type_name() — internal-socket snapshot entries carry a
// numeric socket type, so the AUTO_HWM_DETAIL `socket_type=` token must
// be derived from the numeric enum (not a JS socket instance).
function autoHwmSnapshotSocketTypeName(socketType) {
  return socketTypeName(typeof socketType === 'number' ? socketType : Number(socketType));
}

// C parity: perf_auto_hwm_send_side_visible / perf_auto_hwm_recv_side_visible.
// NOTE: this is the *narrow* spotnode-snapshot rule (SUB/XSUB + recv_ingress/
// control hides send; PUB/XPUB + spot_data/control hides recv), distinct
// from the broader per-socket monitor rule used by hwmSndBufDisplay.
function autoHwmSnapshotSendSideVisible(socketTypeName_, roleName) {
  if ((socketTypeName_ === 'sub' || socketTypeName_ === 'xsub')
      && (roleName === 'recv_ingress' || roleName === 'control')) {
    return false;
  }
  return true;
}

function autoHwmSnapshotRecvSideVisible(socketTypeName_, roleName) {
  if ((socketTypeName_ === 'pub' || socketTypeName_ === 'xpub')
      && (roleName === 'spot_data' || roleName === 'control')) {
    return false;
  }
  return true;
}

function autoHwmEffectiveMsgSize(msgSize) {
  if (msgSize && Number(msgSize) !== 0) {
    return Number(msgSize);
  }
  const value = process.env.PERF_MSG_SIZES || process.env.PERF_MULTI_MSG_SIZES;
  if (!value) {
    return 0;
  }
  const parsed = Number.parseInt(String(value).trim(), 10);
  return Number.isNaN(parsed) ? 0 : parsed;
}

function autoHwmLabelIsControlSnapshot(label) {
  return typeof label === 'string' && label.indexOf('control') >= 0;
}

// C parity: perf_auto_hwm_include_spot_snapshot_row — under a control-scope
// label only the peer control mesh sockets are surfaced.
function autoHwmIncludeSpotSnapshotRow(label, socketName) {
  if (!autoHwmLabelIsControlSnapshot(label)) {
    return true;
  }
  return socketName === 'peer_ctrl_pub' || socketName === 'peer_ctrl_sub';
}

const emittedSpotNodeAutoHwmSnapshots = new Set();
// C keys the snapshot dedup on the spot-node pointer; the JS handle has no
// stringifiable identity, so assign a stable per-node id (one node per
// perf process — scope/transport/msg_size already disambiguate the rest).
const spotNodeIdentityIds = new WeakMap();
let spotNodeIdentitySeq = 0;
function spotNodeIdentity(node) {
  let id = spotNodeIdentityIds.get(node);
  if (id === undefined) {
    spotNodeIdentitySeq += 1;
    id = spotNodeIdentitySeq;
    spotNodeIdentityIds.set(node, id);
  }
  return id;
}

// Faithful port of perf_print_spot_node_auto_hwm_snapshot +
// perf_emit_spot_node_auto_hwm_detail
// (bindings/c/perf/multi/common/perf_multi_runtime.hpp ~L379-509):
// enumerate the spot node's internal sockets and emit one
// AUTO_HWM_DETAIL,...,source=spotnode_snapshot line per visible socket so
// the `## Auto-HWM Detail` collector renders the C `Auto-HWM spotnode:`
// per-size tables (external-router / local-pub / mesh-pub / mesh-xsub /
// peer_ctrl_*). Presentation-only; never touches measured RESULT or the
// termination/2-pass paths.
function emitSpotNodeAutoHwmSnapshot(node, label, transport, msgSize) {
  if (typeof node.internalSockets !== 'function') {
    return false;
  }
  const pattern = process.env.PERF_MULTI_PATTERN || process.env.PERF_PATTERN || 'unknown';
  const component = process.env.PERF_MULTI_COMPONENT || 'process';
  const effectiveTransport = transport || process.env.PERF_MULTI_TRANSPORT || 'unknown';
  const snapshotScope = autoHwmLabelIsControlSnapshot(label) ? 'control' : 'data';
  const dedupKey = [
    String(spotNodeIdentity(node)),
    effectiveTransport,
    String(msgSize || 0),
    snapshotScope
  ].join('|');
  if (emittedSpotNodeAutoHwmSnapshots.has(dedupKey)) {
    return true;
  }
  emittedSpotNodeAutoHwmSnapshots.add(dedupKey);

  let rows;
  try {
    rows = node.internalSockets();
  } catch (err) {
    return false;
  }
  if (!Array.isArray(rows) || rows.length === 0) {
    return true;
  }

  const effectiveMsgSize = autoHwmEffectiveMsgSize(msgSize);
  for (const row of rows) {
    if (!row || row.autoHwmVisible === false) {
      continue;
    }
    // C parity (bindings/c/perf/multi/common/perf_multi_runtime.hpp:488
    // `if (rows[i].auto_hwm_visible == 0) continue;`): in the C perf
    // reference the spot-dispatch `internal_receiver` snapshot socket is
    // always reported with auto_hwm_visible==0 — its SUB is never
    // auto-HWM-activated under C's `perf_create_default_spot_handle`
    // flow, so C's filter drops it and the `Auto-HWM spotnode:` table
    // never contains an `internal_receiver` row (verified: zero
    // occurrences across every C multi reference report). The node
    // `createSpot()` path activates that internal SUB earlier
    // (auto_hwm_enabled / auto_hwm_role become non-zero), so core's
    // auto_hwm_visible_from_snapshot() surfaces it as visible=1 and
    // node would otherwise emit an extra row whose 17-char socket name
    // also widens the MULTI_SPOT Auto-HWM column widths. Match C's
    // reference auto_hwm_visible semantics for this internal transport
    // socket by excluding it here (presentation-only; never touches a
    // measured RESULT or the termination/2-pass paths).
    if (row.socketName === 'internal_receiver') {
      continue;
    }
    if (!autoHwmIncludeSpotSnapshotRow(label, row.socketName)) {
      continue;
    }
    const snapshot = row.snapshot;
    if (!snapshot) {
      continue;
    }
    const typeName = autoHwmSnapshotSocketTypeName(row.socketType);
    const roleName = autoHwmRoleName(snapshot.autoHwmRole);
    // C: owner enum NODE(1)/SPOT(2) -> node|spot, scope shared|per-spot.
    const ownerNode = Number(row.owner) === 1;
    const owner = ownerNode ? 'node' : 'spot';
    const scope = ownerNode ? 'shared' : 'per-spot';
    const sndbuf = autoHwmSnapshotSendSideVisible(typeName, roleName)
      ? String(snapshot.autoHwmEffectiveSndBuf)
      : '0';
    const rcvbuf = autoHwmSnapshotRecvSideVisible(typeName, roleName)
      ? String(snapshot.autoHwmEffectiveRcvBuf)
      : '0';
    console.log(
      'AUTO_HWM_DETAIL'
      + `,pattern=${pattern}`
      + `,transport=${effectiveTransport}`
      + `,component=${component}`
      + `,label=${row.socketName}`
      + `,owner=${owner}`
      + `,owner_id=${row.ownerId}`
      + `,socket=${row.socketName}`
      + `,socket_type=${typeName}`
      + `,msg_size=${effectiveMsgSize}`
      + ',source=spotnode_snapshot'
      + `,enabled=${snapshot.autoHwmEnabled ? 1 : 0}`
      + `,role=${roleName}`
      + `,role_id=${snapshot.autoHwmRole}`
      + `,profile=${autoHwmProfileName(snapshot.autoHwmProfile)}`
      + `,profile_id=${snapshot.autoHwmProfile}`
      + `,policy_class=${autoHwmPolicyClassName(snapshot.autoHwmPolicyClass)}`
      + `,policy_class_id=${snapshot.autoHwmPolicyClass}`
      + `,unit_budget_bytes=${snapshot.autoHwmUnitBudgetBytes}`
      + `,size_cap=${snapshot.autoHwmSizeCap}`
      + `,scope=${scope}`
      + `,sndhwm=${snapshot.autoHwmAppliedSndHwm}`
      + `,rcvhwm=${snapshot.autoHwmAppliedRcvHwm}`
      + `,socket_message_slots=${snapshot.autoHwmSocketMessageSlots}`
      + `,effective_message_bytes=${snapshot.autoHwmEffectiveMessageBytes}`
      + `,effective_sndbuf=${sndbuf}`
      + `,effective_rcvbuf=${rcvbuf}`
      + `,last_recalc_reason=${autoHwmRecalcReasonName(snapshot.autoHwmLastRecalcReason)}`
    );
  }
  return true;
}

function emitMultiSocketHwmDetail(socket, label, transport, msgSize) {
  if (!socket || !autoHwmDetailEnabled()) {
    return;
  }
  // C parity (perf_print_auto_hwm_snapshot ~L577): a `spotnode*` label on
  // a spot-node service handle takes the internal-socket snapshot path
  // instead of the per-socket monitor path, yielding the C
  // `Auto-HWM spotnode:` per-size tables.
  if (typeof label === 'string' && label.indexOf('spotnode') === 0
      && typeof socket.internalSockets === 'function') {
    if (emitSpotNodeAutoHwmSnapshot(socket, label, transport, msgSize)) {
      return;
    }
  }
  // Capability gap (objects without a monitor surface) is not a fault —
  // skip the optional diagnostic.
  if (typeof socket.monitorOpen !== 'function') {
    return;
  }
  // PERF_POLICY § 1.1.4: a failed `monitorOpen` on a socket that DOES
  // support it is a real broken-socket fault and must surface. Only the
  // snapshot read + diagnostic emission is best-effort (it never affects
  // the measured RESULT).
  const monitor = socket.monitorOpen([MonitorEventType.ConnectionReady]);
  try {
    const snapshot = monitor.status();
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

  const fallback = integerEnv('PERF_MULTI_DEFAULT_IO_THREADS', integerEnv('PERF_DEFAULT_IO_THREADS', NaN));
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
  const received = new zlink.TopicMessage();
  return subscribeNoWaitInto(socket, received) ? received : null;
}

function subscribeNoWaitInto(socket, received) {
  try {
    return socket.subscribe(received, RecvFlags.DontWait);
  } catch (error) {
    if (error instanceof zlink.RecvError && error.result === RecvResult.NoData) {
      return false;
    }
    throw error;
  }
}

async function waitForConnectionReady(
  socket,
  connectFn = null,
  timeoutMs = integerEnvPair('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 'PERF_CONNECT_READY_TIMEOUT_MS', 1000)
) {
  return waitForConnectionReadyCount(socket, 1, connectFn, timeoutMs);
}

async function waitForConnectionReadyCount(
  socket,
  expectedCount,
  connectFn = null,
  timeoutMs = integerEnvPair('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 'PERF_CONNECT_READY_TIMEOUT_MS', 1000)
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
        await sleepMs(1);
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
    if (!routed && !Array.isArray(payload) && typeof socket.sendFrom === 'function') {
      return socket.sendFrom(payload, zlink.SendFlags.DontWait);
    }
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

function sleepMs(ms) {
  return new Promise((resolve) => setTimeout(resolve, Math.max(0, ms)));
}

// C parity: bindings/c/perf/multi/common/perf_multi_spot_control.hpp
// publish_control_payload (~536-584). The control PUB->SUB link is a
// slow-joiner channel: a single publish can be dropped/backpressured
// while the peer's subscription is still propagating, and a PUB socket
// emits NO POLLOUT drain wakeup when it has no live subscriber pipe.
// C therefore does NOT wait on a signal-driven `-1` POLLOUT poller for
// control sends — it uses a BOUNDED deadline
// (PERF_MULTI_CONNECT_READY_TIMEOUT_MS, default 1000ms), a blocking
// publish attempt, and a short (<=10ms) timed idle wait on backpressure,
// then RETURNS (false) on timeout so the caller's higher-level handshake
// loop (e.g. wait_msg_size_start_with_ready_republish) can re-publish.
// An infinite signal-driven loop here is the MULTI_SPOT non-termination
// root cause, so this mirrors C exactly: bounded, returns a boolean.
async function publishControlUntilSent(socket, _waiter, topic, payload) {
  const body = Buffer.isBuffer(payload) ? payload : Buffer.from(String(payload));
  const deadlineMs = Math.max(
    1,
    integerEnvPair('PERF_MULTI_CONNECT_READY_TIMEOUT_MS', 'PERF_CONNECT_READY_TIMEOUT_MS', 1000)
  );
  const deadline = Date.now() + deadlineMs;
  while (Date.now() < deadline) {
    if (trySocketPublish(socket, topic, body)) {
      return true;
    }
    const remaining = deadline - Date.now();
    if (remaining <= 0) {
      break;
    }
    await sleepMs(Math.min(remaining, 10));
  }
  return false;
}

function createCallbackEventWaiter(register) {
  let pending = 0;
  const waiters = [];
  register(() => {
    const resolve = waiters.shift();
    if (resolve) {
      resolve();
      return;
    }
    pending += 1;
  });
  return {
    async wait() {
      if (pending > 0) {
        pending -= 1;
        return;
      }
      await new Promise((resolve) => waiters.push(resolve));
    }
  };
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

async function waitForRunnerStart(msgSize) {
  const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
  try {
    for await (const line of rl) {
      if (line === `START,${msgSize}` || line === 'STOP' || line === 'QUIT') {
        return line;
      }
    }
    return null;
  } finally {
    rl.close();
  }
}

async function waitForControlStart(controlSub, waiter, msgSize) {
  for (;;) {
    while (true) {
      const received = subscribeNoWait(controlSub);
      if (!received) {
        break;
      }
      try {
        const payloadText = received.parts[0].data().toString('utf8');
        if (payloadText === `START,${msgSize}`) {
          return;
        }
      } finally {
        received.close();
      }
    }
    await waiter.wait(POLLIN);
  }
}

function createSocketEventWaiter(socket, events) {
  const poller = new zlink.Poller();
  poller.add(socket, pollEvents(events), 0);
  const eventBuffer = new zlink.PollEvents(1);

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
          ready = waitPollerOne(poller, eventBuffer, -1);
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
      eventBuffer.close();
      poller.close();
    }
  };
}

module.exports = {
  POLLIN,
  POLLOUT,
  POLLCOMPLETION,
  applyContextPolicy,
  applyAutoHwmMsgUnit,
  applySpotNodeAdmission,
  applySocketPolicy,
  createCallbackEventWaiter,
  createSocketEventWaiter,
  emitMultiSocketHwmDetail,
  pollEvents,
  pollEventHas,
  waitPollerOne,
  publishControlUntilSent,
  recvNoWait,
  recvNoWaitInto,
  sendStopTokenOnce,
  subscribeNoWait,
  subscribeNoWaitInto,
  trySocketPublish,
  trySocketSend,
  waitForControlStart,
  waitForRunnerControlConnected,
  waitForRunnerStart,
  waitForConnectionReadyCount,
  waitForConnectionReady
};
