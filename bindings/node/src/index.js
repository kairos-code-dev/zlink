// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('fs');
const path = require('path');

function prependPathEntries(entries) {
  const existing = (process.env.PATH || '').split(';').filter(Boolean);
  for (const entry of entries) {
    if (!entry || !fs.existsSync(entry)) continue;
    if (!existing.includes(entry)) existing.unshift(entry);
  }
  process.env.PATH = existing.join(';');
}

function loadNative() {
  try {
    if (process.platform === 'linux') {
      const addonDir = path.join(__dirname, '..', 'build', 'Release');
      const coreDir = path.join(__dirname, '..', '..', '..', 'core', 'build', 'lib');
      const coreAltDir = path.join(__dirname, '..', '..', 'build_cpp', 'lib');
      const addonLib = path.join(addonDir, 'libzlink.so.5');
      const coreLib = path.join(coreDir, 'libzlink.so.5');
      const coreAltLib = path.join(coreAltDir, 'libzlink.so.5');
      if (!fs.existsSync(addonLib)) {
        let sourceLib = null;
        if (fs.existsSync(coreAltLib)) {
          sourceLib = coreAltLib;
        } else if (fs.existsSync(coreLib)) {
          sourceLib = coreLib;
        }
        if (sourceLib) {
          try {
            fs.symlinkSync(sourceLib, addonLib);
          } catch (err) {
            if (!err || err.code !== 'EEXIST') throw err;
          }
        }
      }
      const existing = (process.env.LD_LIBRARY_PATH || '').split(':').filter(Boolean);
      for (const entry of [coreAltDir, coreDir, addonDir]) {
        if (!existing.includes(entry)) existing.unshift(entry);
      }
      process.env.LD_LIBRARY_PATH = existing.join(':');
    }
    return require('../build/Release/zlink.node');
  } catch (_) {
    try {
      const prebuiltDir = path.join(__dirname, '..', 'prebuilds', `${process.platform}-${process.arch}`);
      const prebuilt = path.join(prebuiltDir, 'zlink.node');
      if (process.platform === 'win32') {
        prependPathEntries([
          prebuiltDir,
          process.env.ZLINK_OPENSSL_BIN,
          process.env.OPENSSL_BIN,
          'C:\\Program Files\\OpenSSL-Win64\\bin',
          'C:\\Program Files\\Git\\mingw64\\bin',
          'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\TeamFoundation\\Team Explorer\\Git\\mingw64\\bin'
        ]);
      }
      return require(prebuilt);
    } catch (_) {
      return null;
    }
  }
}

const native = loadNative();

function requireNative() {
  if (!native) throw new Error('zlink native addon not found. Build with node-gyp.');
  return native;
}

function normalizeBufferLike(value, label = 'value') {
  if (Buffer.isBuffer(value)) return value;
  if (value instanceof Uint8Array) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  if (typeof value === 'string') return Buffer.from(value);
  throw new TypeError(`${label} must be Buffer, Uint8Array, or string`);
}

const SocketType = Object.freeze({
  PAIR: 0x1001, PUB: 0x1002, SUB: 0x1003, DEALER: 0x1004,
  ROUTER: 0x1005, XPUB: 0x1006, XSUB: 0x1007, STREAM: 0x1008
});

const ContextOption = Object.freeze({
  IO_THREADS: 1, MAX_SOCKETS: 2, SOCKET_LIMIT: 3,
  THREAD_PRIORITY: 3, THREAD_SCHED_POLICY: 4, MAX_MSGSZ: 5,
  MSG_T_SIZE: 6, THREAD_AFFINITY_CPU_ADD: 7,
  THREAD_AFFINITY_CPU_REMOVE: 8, THREAD_NAME_PREFIX: 9,
  BLOCKY: 10
});

const SocketOption = Object.freeze({
  AFFINITY: 0x3001, RATE: 0x3003, RECOVERY_IVL: 0x3004,
  SNDBUF: 0x3005, RCVBUF: 0x3006, FD: 0x3007, EVENTS: 0x3008,
  TYPE: 0x3009, LINGER: 0x300A, RECONNECT_IVL: 0x300B,
  BACKLOG: 0x300C, RECONNECT_IVL_MAX: 0x300D, MAXMSGSIZE: 0x300E,
  SNDHWM: 0x300F, RCVHWM: 0x3010, MULTICAST_HOPS: 0x3011,
  RCVTIMEO: 0x3012, SNDTIMEO: 0x3013, LAST_ENDPOINT: 0x3014,
  TCP_KEEPALIVE: 0x3015, TCP_KEEPALIVE_CNT: 0x3016,
  TCP_KEEPALIVE_IDLE: 0x3017, TCP_KEEPALIVE_INTVL: 0x3018,
  IMMEDIATE: 0x3019, IPV6: 0x301A, CONFLATE: 0x301B,
  TOS: 0x301C, HANDSHAKE_IVL: 0x301D, BLOCKY: 0x301E,
  INVERT_MATCHING: 0x3020, HEARTBEAT_IVL: 0x3021,
  HEARTBEAT_TTL: 0x3022, HEARTBEAT_TIMEOUT: 0x3023,
  CONNECT_TIMEOUT: 0x3024, TCP_MAXRT: 0x3025,
  MULTICAST_MAXTPDU: 0x3026, BINDTODEVICE: 0x3027,
  TLS_CERT: 0x3028, TLS_KEY: 0x3029, TLS_CA: 0x302A,
  TLS_VERIFY: 0x302B, TLS_REQUIRE_CLIENT_CERT: 0x302C,
  TLS_HOSTNAME: 0x302D, TLS_TRUST_SYSTEM: 0x302E,
  TLS_PASSWORD: 0x302F, ZMP_METADATA: 0x3030,
  TCP_NODELAY: 0x3031, DISCOVERY_METADATA_MAX_SIZE: 0x3032,
  ROUTING_ID: 5, SUBSCRIBE: 6, UNSUBSCRIBE: 7,
  ROUTER_MANDATORY: 0x3101, ROUTER_HANDOVER: 0x3102,
  PROBE_ROUTER: 0x3103, CONNECT_ROUTING_ID: 0x3104,
  XPUB_VERBOSE: 0x3301, XPUB_VERBOSER: 0x3302, XPUB_MANUAL: 0x3303,
  XPUB_MANUAL_LAST_VALUE: 0x3304, XPUB_NODROP: 0x3305,
  XPUB_WELCOME_MSG: 0x3306, TOPICS_COUNT: 0x3307,
  ONLY_FIRST_SUBSCRIBE: 0x3308
});

const SendFlag = Object.freeze({ NONE: 0, DONTWAIT: 0x0001, SNDMORE: 0x0002 });
const ReceiveFlag = Object.freeze({ NONE: 0, DONTWAIT: 0x0001 });
const StreamDispatchMode = Object.freeze({ NONE: 0, LEN32BE: 1 });
const ErrorCode = Object.freeze({
  EFSM: 156384763, ENOCOMPATPROTO: 156384764, ETERM: 156384765, EMTHREAD: 156384766
});
const ProtocolError = Object.freeze({
  ZMP_MALFORMED_COMMAND_HELLO: 0x10000013
});
const MonitorEvent = Object.freeze({
  CONNECTED: 0x0001, CONNECT_DELAYED: 0x0002, CONNECT_RETRIED: 0x0004,
  LISTENING: 0x0008, BIND_FAILED: 0x0010, ACCEPTED: 0x0020,
  ACCEPT_FAILED: 0x0040, CLOSED: 0x0080, CLOSE_FAILED: 0x0100,
  DISCONNECTED: 0x0200, MONITOR_STOPPED: 0x0400,
  HANDSHAKE_FAILED_NO_DETAIL: 0x0800,
  CONNECTION_READY_CHANGED: 0x1000, HANDSHAKE_FAILED_PROTOCOL: 0x2000,
  HANDSHAKE_FAILED_AUTH: 0x4000,
  SUB_DELIVERY_READY_CHANGED: 0x8000, PUB_DELIVERY_READY_CHANGED: 0x10000,
  ALL: 0xFFFF
});
const DisconnectReason = Object.freeze({
  UNKNOWN: 0, HANDSHAKE_FAILED: 3, TRANSPORT_ERROR: 4, CTX_TERM: 5
});
const PollEvent = Object.freeze({ POLLIN: 1, POLLOUT: 2, POLLERR: 4, POLLPRI: 8 });
const ServiceType = Object.freeze({ SPOT: 0x3002, SOCKET: 0x3003 });
const ServiceRole = Object.freeze({
  INVALID: 0, SPOT: 2, ROUTER: 3, DEALER: 4, PUB: 5, SUB: 6
});
const ServiceKind = Object.freeze({
  DISCOVERY: 1, SPOT_SUB: 3, SPOT_PUB: 4, SOCKET: 5
});
const RegistrySocketRole = Object.freeze({ PUB: 5, ROUTER: 3, PEER_SUB: 6 });
const DiscoverySocketRole = Object.freeze({ SUB: 6 });
const SpotNodeSocketRole = Object.freeze({ NODE: 0, PUB: 1, SUB: 2, DEALER: 3 });
const SpotNodeOption = Object.freeze({ PUB_MODE: 1, PUB_QUEUE_HWM: 2, PUB_QUEUE_FULL_POLICY: 3 });
const SpotNodePubMode = Object.freeze({ SYNC: 0, ASYNC: 1 });
const SpotNodePubQueueFullPolicy = Object.freeze({ EAGAIN: 0, DROP: 1 });
const SpotSocketRole = Object.freeze({ PUB: 1, SUB: 2 });
const MonitorSourceKind = Object.freeze({ SOCKET: 1, SPOT_PUB: 3, SPOT_SUB: 4 });
const MonitorState = Object.freeze({
  READY: 1 << 0, BOUND_READY: 1 << 1, SEND_READY: 1 << 2, CLOSED: 1 << 3
});
const MonitorSnapshotDetail = Object.freeze({
  READY_COUNT: 1 << 0, SND_PENDING_MSGS: 1 << 1, RCV_PENDING_MSGS: 1 << 2
});
const ServiceMonitorEvent = Object.freeze({
  ERROR: 1 << 4,
  CLOSED: 1 << 17,
  DISCOVERY_READY_CHANGED: 1 << 0,
  DISCOVERY_SERVICE_UP: 1 << 5,
  DISCOVERY_SERVICE_DOWN: 1 << 6,
  DISCOVERY_PROVIDERS_CHANGED: 1 << 7,
  SPOT_READY_CHANGED: 1 << 0,
  SPOT_FILTER_APPLIED: 1 << 13,
  SPOT_SUBSCRIPTION_READY_CHANGED: 1 << 14,
  SPOT_PUB_DELIVERY_READY_CHANGED: 1 << 18,
  SPOT_SUB_DELIVERY_READY_CHANGED: 1 << 19,
  SPOT_FIRST_DELIVERY_READY_CHANGED: 1 << 20
});
const TopologySource = Object.freeze({ MANUAL: 1, DISCOVERY: 2, REGISTRY: 3 });
const TopologyState = Object.freeze({
  DISCOVERED: 1, CONNECTING: 2, READY: 3, LOST: 4, ERROR: 5, STOPPED: 6
});

class Context {
  constructor() {
    this._native = requireNative().ctxNew();
  }

  close() {
    if (!this._native) return;
    requireNative().ctxTerm(this._native);
    this._native = null;
  }
}

class Message {
  constructor(buffer, borrowed) {
    this._buffer = buffer;
    this._borrowed = borrowed === true;
    Object.freeze(this);
  }

  static copyOf(data, encoding = 'utf8') {
    if (typeof data === 'string') return new Message(Buffer.from(data, encoding), false);
    return new Message(Buffer.from(normalizeBufferLike(data, 'data')), false);
  }

  static wrap(buffer) {
    return new Message(normalizeBufferLike(buffer, 'buffer'), true);
  }

  static empty() {
    return new Message(Buffer.alloc(0), false);
  }

  toBuffer() {
    return this._borrowed ? this._buffer : Buffer.from(this._buffer);
  }

  byteLength() {
    return this._buffer.length;
  }
}

class Received {
  constructor(parts, routingId = null, hasMore = false) {
    this.parts = Object.freeze(parts.slice());
    this.routingId = routingId;
    this.hasMore = hasMore === true;
  }

  close() {}
}

class Socket {
  constructor(ctx, type) {
    this._native = requireNative().socketNew(ctx._native, type);
    this._own = true;
  }

  bind(endpoint) {
    requireNative().socketBind(this._native, endpoint);
  }

  connect(endpoint) {
    requireNative().socketConnect(this._native, endpoint);
  }

  send(message, flags = 0) {
    const payload = message instanceof Message
      ? message._buffer
      : normalizeBufferLike(message, 'message');
    return requireNative().socketSend(this._native, payload, flags | 0);
  }

  sendParts(parts, flags = 0) {
    if (!Array.isArray(parts)) throw new TypeError('parts must be an array');
    const buffers = parts.map((part, index) => {
      if (part instanceof Message) return part._buffer;
      return normalizeBufferLike(part, `parts[${index}]`);
    });
    return requireNative().socketSendParts(this._native, buffers, flags | 0);
  }

  sendFrom(buffer, length, flags = 0) {
    const source = normalizeBufferLike(buffer, 'buffer');
    return requireNative().socketSendFrom(this._native, source, length | 0, flags | 0);
  }

  recv(arg0 = 0, arg1 = undefined) {
    if (typeof arg1 === 'number') {
      return requireNative().socketRecv(this._native, arg0 | 0, arg1 | 0);
    }
    if (typeof arg0 === 'number' && arg0 > ReceiveFlag.DONTWAIT) {
      return requireNative().socketRecv(this._native, arg0 | 0, 0);
    }
    const raw = requireNative().socketRecvMessage(this._native, arg0 | 0);
    return new Received(raw.parts, raw.routingId ?? null, raw.hasMore === true);
  }

  recvInto(buffer, flags = 0) {
    return requireNative().socketRecvInto(this._native, normalizeBufferLike(buffer, 'buffer'), flags | 0);
  }

  recvMsgInto(buffer, flags = 0) {
    return requireNative().socketRecvMsgInto(this._native, normalizeBufferLike(buffer, 'buffer'), flags | 0);
  }

  setSockOpt(option, value) {
    requireNative().socketSetOpt(this._native, option | 0, normalizeBufferLike(value, 'value'));
  }

  getSockOpt(option) {
    return requireNative().socketGetOpt(this._native, option | 0);
  }

  setOption(option, value) {
    this.setSockOpt(option, value);
  }

  getOption(option) {
    return this.getSockOpt(option);
  }

  setRoutingId(routingId) {
    this.setSockOpt(SocketOption.ROUTING_ID, routingId);
  }

  getRoutingId() {
    return this.getSockOpt(SocketOption.ROUTING_ID);
  }

  subscribe(filter) {
    this.setSockOpt(SocketOption.SUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  unsubscribe(filter) {
    this.setSockOpt(SocketOption.UNSUBSCRIBE, normalizeBufferLike(filter, 'filter'));
  }

  monitorOpen(events) {
    return new MonitorSocket(requireNative().monitorOpen(this._native, events | 0));
  }

  streamAttach(handler, mode = 0) {
    if (typeof handler !== 'function') throw new TypeError('streamAttach handler must be a function');
    requireNative().socketStreamAttach(this._native, handler, mode | 0);
  }

  streamAttachRaw(handler) {
    this.streamAttach(handler, StreamDispatchMode.NONE);
  }

  streamAttachLen32be(handler) {
    this.streamAttach(handler, StreamDispatchMode.LEN32BE);
  }

  streamDetach() {
    requireNative().socketStreamDetach(this._native);
  }

  streamPeerRoutingId(index = 0) {
    return requireNative().socketStreamPeerRoutingId(this._native, index | 0);
  }

  streamSend(routingId, payload, flags = 0) {
    return requireNative().socketStreamSend(
      this._native,
      normalizeBufferLike(routingId, 'routingId'),
      normalizeBufferLike(payload, 'payload'),
      flags | 0
    );
  }

  close() {
    if (!this._native) return;
    try {
      requireNative().socketStreamDetach(this._native);
    } catch (_) {}
    if (this._own) requireNative().socketClose(this._native);
    this._native = null;
  }
}

class MonitorSocket {
  constructor(handle) {
    this._native = handle;
  }

  recv() {
    return requireNative().monitorRecv(this._native);
  }

  snapshot() {
    return requireNative().monitorSnapshot(this._native);
  }

  close() {
    if (!this._native) return;
    requireNative().monitorClose(this._native);
    this._native = null;
  }
}

class ServiceMonitor {
  constructor(handle) {
    this._native = handle;
  }

  recv() {
    return requireNative().serviceMonitorRecv(this._native);
  }

  snapshot() {
    return requireNative().monitorSnapshot(this._native);
  }

  close() {
    if (!this._native) return;
    requireNative().monitorClose(this._native);
    this._native = null;
  }
}

class Poller {
  constructor() {
    this._items = [];
  }

  addSocket(socket, events) {
    this._items.push({ socket: socket._native, fd: 0, events: events | 0 });
  }

  poll(timeoutMs) {
    return requireNative().poll(this._items, timeoutMs | 0);
  }
}

class Registry {
  constructor(ctx) {
    this._native = requireNative().registryNew(ctx._native);
    this._bound = false;
  }

  bind(pub, router) {
    if (this._bound) {
      throw new Error('Registry.bind may only be called once on the aligned public API');
    }
    requireNative().registrySetEndpoints(this._native, pub, router);
    this._bound = true;
  }

  setId(id) {
    requireNative().registrySetId(this._native, id | 0);
  }

  addPeer(pub) {
    requireNative().registryAddPeer(this._native, pub);
  }

  setHeartbeat(intervalMs, timeoutMs) {
    requireNative().registrySetHeartbeat(this._native, intervalMs | 0, timeoutMs | 0);
  }

  setBroadcastInterval(intervalMs) {
    requireNative().registrySetBroadcastInterval(this._native, intervalMs | 0);
  }

  setEndpoints() {
    throw new Error('Registry.setEndpoints is removed from the aligned public API. Use Registry.bind(pub, router).');
  }

  start() {
    throw new Error('Registry.start is removed from the aligned public API. Use Registry.bind(pub, router).');
  }

  statusSnapshot() {
    return requireNative().registryStatusSnapshot(this._native);
  }

  serviceSummarySnapshot() {
    return requireNative().registryServiceSummarySnapshot(this._native);
  }

  topologySnapshot() {
    return requireNative().registryTopologySnapshot(this._native);
  }

  topologyQuery(filter) {
    return requireNative().registryTopologyQuery(this._native, filter);
  }

  memberPeers(serviceType, serviceName = '') {
    return requireNative().registryMemberPeers(this._native, serviceType, serviceName);
  }

  setSockOpt(role, option, value) {
    void role;
    void option;
    void value;
    throw new Error('Registry.setSockOpt is not available on the aligned public API');
  }

  close() {
    if (!this._native) return;
    requireNative().registryDestroy(this._native);
    this._native = null;
  }
}

class RegistryQueryClient {
  constructor(ctx) {
    this._native = requireNative().registryQueryClientNew(ctx._native);
  }

  connect(endpoint) {
    requireNative().registryQueryClientConnect(this._native, endpoint);
  }

  snapshot(filter) {
    return requireNative().registryQuerySnapshot(this._native, filter);
  }

  close() {
    if (!this._native) return;
    requireNative().registryQueryDestroy(this._native);
    this._native = null;
  }
}

class Discovery {
  constructor(ctx, serviceType, serviceName) {
    if (typeof serviceName !== 'string' || serviceName.length === 0) {
      throw new TypeError('Discovery serviceName must be a non-empty string');
    }
    this._native = requireNative().discoveryNew(ctx._native, serviceType, serviceName);
    this.serviceType = serviceType;
    this.serviceName = serviceName;
  }

  connectRegistry(endpoint) {
    requireNative().discoveryConnectRegistry(this._native, endpoint);
  }

  receiverCount() {
    return requireNative().discoveryProviderCount(this._native);
  }

  setValue(value) {
    requireNative().discoverySetValue(this._native, value);
  }

  value() {
    return requireNative().discoveryGetValue(this._native);
  }

  setMetadata(metadata) {
    requireNative().discoverySetMetadata(this._native, normalizeBufferLike(metadata, 'metadata'));
  }

  metadata() {
    return requireNative().discoveryGetMetadata(this._native);
  }

  memberPeers() {
    return requireNative().discoveryGetProviders(this._native);
  }

  serviceAvailable() {
    return requireNative().discoveryServiceAvailable(this._native);
  }

  openMonitor(events = ServiceMonitorEvent.ERROR | ServiceMonitorEvent.CLOSED) {
    return new ServiceMonitor(requireNative().discoveryOpenMonitor(this._native, events | 0));
  }

  setTlsClient(caCert, hostname, trustSystem = 0) {
    requireNative().discoverySetTlsClient(this._native, caCert || '', hostname || '', trustSystem | 0);
  }

  close() {
    if (!this._native) return;
    requireNative().discoveryDestroy(this._native);
    this._native = null;
  }
}

class Receiver {
  constructor() {
    throw new Error('Receiver is removed from the aligned public API. Use Socket + Discovery or SpotNode instead.');
  }
}

class SpotNode {
  constructor(ctx) {
    this._native = requireNative().spotNodeNew(ctx._native);
  }

  bind(endpoint) {
    requireNative().spotNodeBind(this._native, endpoint);
  }

  connectPeerPub(endpoint) {
    requireNative().spotNodeConnectPeerPub(this._native, endpoint);
  }

  disconnectPeerPub(endpoint) {
    requireNative().spotNodeDisconnectPeerPub(this._native, endpoint);
  }

  attachDiscovery(discovery) {
    requireNative().spotNodeSetDiscovery(this._native, discovery._native);
  }

  setDiscovery(discovery) {
    void discovery;
    throw new Error('SpotNode.setDiscovery is removed from the aligned public API. Use attachDiscovery(discovery).');
  }

  register() {
    throw new Error('SpotNode.register is removed from the aligned public API. Attach a Discovery instead.');
  }

  unregister() {
    throw new Error('SpotNode.unregister is removed from the aligned public API.');
  }

  setTlsServer(cert, key, requireClient = 0) {
    requireNative().spotNodeSetTlsServer(this._native, cert, key, requireClient | 0);
  }

  setTlsClient(ca, host, trust = 0) {
    requireNative().spotNodeSetTlsClient(this._native, ca, host, trust | 0);
  }

  statusSnapshot() {
    return requireNative().spotNodeStatusSnapshot(this._native);
  }

  peersSnapshot() {
    return requireNative().spotNodePeersSnapshot(this._native);
  }

  subjectsSnapshot() {
    return requireNative().spotNodeSubjectsSnapshot(this._native);
  }

  openMonitor(events = ServiceMonitorEvent.ERROR
    | ServiceMonitorEvent.CLOSED
    | ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED) {
    return new ServiceMonitor(requireNative().spotNodeOpenMonitor(this._native, events | 0));
  }

  setSockOpt() {
    throw new Error('SpotNode.setSockOpt is not available on the aligned public API');
  }

  pubSocket() {
    throw new Error('SpotNode.pubSocket is not available on the aligned public API');
  }

  subSocket() {
    throw new Error('SpotNode.subSocket is not available on the aligned public API');
  }

  pubPeers() {
    throw new Error('SpotNode.pubPeers is not available on the aligned public API');
  }

  subPeers() {
    throw new Error('SpotNode.subPeers is not available on the aligned public API');
  }

  close() {
    if (!this._native) return;
    requireNative().spotNodeDestroy(this._native);
    this._native = null;
  }
}

class Spot {
  constructor(ctx) {
    this._native = requireNative().spotNew(ctx._native);
  }

  publish(topic, payloadOrParts, flags = 0) {
    if (Array.isArray(payloadOrParts)) {
      requireNative().spotPublish(
        this._native,
        topic,
        payloadOrParts.map((part, index) => normalizeBufferLike(part instanceof Message ? part._buffer : part, `payloadOrParts[${index}]`)),
        flags | 0
      );
      return;
    }
    const payload = payloadOrParts instanceof Message ? payloadOrParts._buffer : normalizeBufferLike(payloadOrParts, 'payload');
    requireNative().spotPublish(this._native, topic, payload, flags | 0);
  }

  subscribe(topic) {
    requireNative().spotSubscribe(this._native, topic);
  }

  subscribePattern(pattern) {
    requireNative().spotSubscribePattern(this._native, pattern);
  }

  unsubscribe(topicOrPattern) {
    requireNative().spotUnsubscribe(this._native, topicOrPattern);
  }

  recv(flags = 0) {
    return requireNative().spotRecv(this._native, flags | 0);
  }

  openMonitor(events = ServiceMonitorEvent.ERROR | ServiceMonitorEvent.CLOSED) {
    return new ServiceMonitor(requireNative().spotOpenMonitor(this._native, events | 0));
  }

  close() {
    if (!this._native) return;
    requireNative().spotDestroy(this._native);
    this._native = null;
  }
}

function version() {
  return requireNative().version();
}

module.exports = {
  version,
  SERVICE_TYPE_SPOT: ServiceType.SPOT,
  SERVICE_TYPE_SOCKET: ServiceType.SOCKET,
  SocketType,
  ContextOption,
  SocketOption,
  SendFlag,
  ReceiveFlag,
  StreamDispatchMode,
  ErrorCode,
  ProtocolError,
  MonitorEvent,
  DisconnectReason,
  PollEvent,
  ServiceType,
  ServiceRole,
  ServiceKind,
  RegistrySocketRole,
  DiscoverySocketRole,
  SpotNodeSocketRole,
  SpotNodeOption,
  SpotNodePubMode,
  SpotNodePubQueueFullPolicy,
  SpotSocketRole,
  MonitorSourceKind,
  MonitorState,
  MonitorSnapshotDetail,
  ServiceMonitorEvent,
  TopologySource,
  TopologyState,
  Context,
  Message,
  Received,
  Socket,
  MonitorSocket,
  ServiceMonitor,
  Poller,
  Registry,
  RegistryQueryClient,
  Discovery,
  SpotNode,
  Spot
};
