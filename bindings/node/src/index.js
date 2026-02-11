// SPDX-License-Identifier: MPL-2.0

'use strict';

const fs = require('fs');
const path = require('path');

function prependPathEntries(entries) {
  const existing = (process.env.PATH || '').split(';').filter(Boolean);
  for (const entry of entries) {
    if (!entry || !fs.existsSync(entry)) continue;
    if (!existing.includes(entry)) {
      existing.unshift(entry);
    }
  }
  process.env.PATH = existing.join(';');
}

function loadNative() {
  try {
    if (process.platform === 'linux') {
      const addonDir = path.join(__dirname, '..', 'build', 'Release');
      const coreDir = path.join(__dirname, '..', '..', 'build_cpp', 'lib');
      const existing = process.env.LD_LIBRARY_PATH || '';
      const entries = existing.split(':').filter(Boolean);
      if (!entries.includes(addonDir)) entries.unshift(addonDir);
      if (!entries.includes(coreDir)) entries.unshift(coreDir);
      process.env.LD_LIBRARY_PATH = entries.join(':');
    }
    return require('../build/Release/zlink.node');
  } catch (err) {
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
    } catch (err2) {
      return null;
    }
  }
}

const native = loadNative();

// --- Enum constant objects ---

const SocketType = Object.freeze({
  PAIR: 0, PUB: 1, SUB: 2, DEALER: 5, ROUTER: 6,
  XPUB: 9, XSUB: 10, STREAM: 11
});

const ContextOption = Object.freeze({
  IO_THREADS: 1, MAX_SOCKETS: 2, SOCKET_LIMIT: 3,
  THREAD_PRIORITY: 3, THREAD_SCHED_POLICY: 4, MAX_MSGSZ: 5,
  MSG_T_SIZE: 6, THREAD_AFFINITY_CPU_ADD: 7,
  THREAD_AFFINITY_CPU_REMOVE: 8, THREAD_NAME_PREFIX: 9
});

const SocketOption = Object.freeze({
  AFFINITY: 4, ROUTING_ID: 5, SUBSCRIBE: 6, UNSUBSCRIBE: 7,
  RATE: 8, RECOVERY_IVL: 9, SNDBUF: 11, RCVBUF: 12,
  RCVMORE: 13, FD: 14, EVENTS: 15, TYPE: 16, LINGER: 17,
  RECONNECT_IVL: 18, BACKLOG: 19, RECONNECT_IVL_MAX: 21,
  MAXMSGSIZE: 22, SNDHWM: 23, RCVHWM: 24, MULTICAST_HOPS: 25,
  RCVTIMEO: 27, SNDTIMEO: 28, LAST_ENDPOINT: 32,
  ROUTER_MANDATORY: 33, TCP_KEEPALIVE: 34, TCP_KEEPALIVE_CNT: 35,
  TCP_KEEPALIVE_IDLE: 36, TCP_KEEPALIVE_INTVL: 37, IMMEDIATE: 39,
  XPUB_VERBOSE: 40, IPV6: 42, PROBE_ROUTER: 51, CONFLATE: 54,
  ROUTER_HANDOVER: 56, TOS: 57, CONNECT_ROUTING_ID: 61,
  HANDSHAKE_IVL: 66, XPUB_NODROP: 69, BLOCKY: 70,
  XPUB_MANUAL: 71, XPUB_WELCOME_MSG: 72, INVERT_MATCHING: 74,
  HEARTBEAT_IVL: 75, HEARTBEAT_TTL: 76, HEARTBEAT_TIMEOUT: 77,
  XPUB_VERBOSER: 78, CONNECT_TIMEOUT: 79, TCP_MAXRT: 80,
  MULTICAST_MAXTPDU: 84, USE_FD: 89, REQUEST_TIMEOUT: 90,
  REQUEST_CORRELATE: 91, BINDTODEVICE: 92, TLS_CERT: 95,
  TLS_KEY: 96, TLS_CA: 97, TLS_VERIFY: 98,
  TLS_REQUIRE_CLIENT_CERT: 99, TLS_HOSTNAME: 100,
  TLS_TRUST_SYSTEM: 101, TLS_PASSWORD: 102,
  XPUB_MANUAL_LAST_VALUE: 98, ONLY_FIRST_SUBSCRIBE: 108,
  TOPICS_COUNT: 116, ZMP_METADATA: 117
});

const SendFlag = Object.freeze({
  NONE: 0, DONTWAIT: 1, SNDMORE: 2
});

const ReceiveFlag = Object.freeze({
  NONE: 0, DONTWAIT: 1
});

const MonitorEvent = Object.freeze({
  CONNECTED: 0x0001, CONNECT_DELAYED: 0x0002,
  CONNECT_RETRIED: 0x0004, LISTENING: 0x0008,
  BIND_FAILED: 0x0010, ACCEPTED: 0x0020,
  ACCEPT_FAILED: 0x0040, CLOSED: 0x0080,
  CLOSE_FAILED: 0x0100, DISCONNECTED: 0x0200,
  MONITOR_STOPPED: 0x0400,
  HANDSHAKE_FAILED_NO_DETAIL: 0x0800,
  CONNECTION_READY: 0x1000,
  HANDSHAKE_FAILED_PROTOCOL: 0x2000,
  HANDSHAKE_FAILED_AUTH: 0x4000,
  ALL: 0xFFFF
});

const DisconnectReason = Object.freeze({
  UNKNOWN: 0, LOCAL: 1, REMOTE: 2,
  HANDSHAKE_FAILED: 3, TRANSPORT_ERROR: 4, CTX_TERM: 5
});

const PollEvent = Object.freeze({
  POLLIN: 1, POLLOUT: 2, POLLERR: 4, POLLPRI: 8
});

const ServiceType = Object.freeze({
  GATEWAY: 1, SPOT: 2
});

const GatewayLbStrategy = Object.freeze({
  ROUND_ROBIN: 0, WEIGHTED: 1
});

const RegistrySocketRole = Object.freeze({
  PUB: 1, ROUTER: 2, PEER_SUB: 3
});

const DiscoverySocketRole = Object.freeze({
  SUB: 1
});

const GatewaySocketRole = Object.freeze({
  ROUTER: 1
});

const ReceiverSocketRole = Object.freeze({
  ROUTER: 1, DEALER: 2
});

const SpotNodeSocketRole = Object.freeze({
  PUB: 1, SUB: 2, DEALER: 3
});

const SpotSocketRole = Object.freeze({
  PUB: 1, SUB: 2
});

function requireNative() {
  if (!native) {
    throw new Error('zlink native addon not found. Build with node-gyp.');
  }
  return native;
}

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

  send(buf, flags = 0) {
    const b = Buffer.isBuffer(buf) ? buf : Buffer.from(buf);
    return requireNative().socketSend(this._native, b, flags);
  }

  recv(size, flags = 0) {
    return requireNative().socketRecv(this._native, size, flags);
  }

  setSockOpt(option, value) {
    const b = Buffer.isBuffer(value) ? value : Buffer.from(value);
    requireNative().socketSetOpt(this._native, option, b);
  }

  getSockOpt(option) {
    return requireNative().socketGetOpt(this._native, option);
  }

  monitorOpen(events) {
    return new MonitorSocket(requireNative().monitorOpen(this._native, events));
  }

  close() {
    if (!this._native) return;
    if (this._own) {
      requireNative().socketClose(this._native);
    }
    this._native = null;
  }
}

class MonitorSocket {
  constructor(handle) {
    this._native = handle;
  }

  recv(flags = 0) {
    return requireNative().monitorRecv(this._native, flags);
  }

  close() {
    if (!this._native) return;
    requireNative().socketClose(this._native);
    this._native = null;
  }
}

class Poller {
  constructor() {
    this._items = [];
  }

  addSocket(socket, events) {
    this._items.push({ socket: socket._native, fd: 0, events });
  }

  poll(timeoutMs) {
    return requireNative().poll(this._items, timeoutMs);
  }
}

class Registry {
  constructor(ctx) {
    this._native = requireNative().registryNew(ctx._native);
  }
  setEndpoints(pub, router) { requireNative().registrySetEndpoints(this._native, pub, router); }
  setId(id) { requireNative().registrySetId(this._native, id); }
  addPeer(pub) { requireNative().registryAddPeer(this._native, pub); }
  setHeartbeat(intervalMs, timeoutMs) { requireNative().registrySetHeartbeat(this._native, intervalMs, timeoutMs); }
  setBroadcastInterval(intervalMs) { requireNative().registrySetBroadcastInterval(this._native, intervalMs); }
  start() { requireNative().registryStart(this._native); }
  setSockOpt(role, option, value) {
    const b = Buffer.isBuffer(value) ? value : Buffer.from(value);
    requireNative().registrySetSockOpt(this._native, role, option, b);
  }
  close() { if (!this._native) return; requireNative().registryDestroy(this._native); this._native = null; }
}

class Discovery {
  constructor(ctx, serviceType) { this._native = requireNative().discoveryNew(ctx._native, serviceType); }
  connectRegistry(pub) { requireNative().discoveryConnectRegistry(this._native, pub); }
  subscribe(service) { requireNative().discoverySubscribe(this._native, service); }
  unsubscribe(service) { requireNative().discoveryUnsubscribe(this._native, service); }
  receiverCount(service) { return requireNative().discoveryProviderCount(this._native, service); }
  serviceAvailable(service) { return requireNative().discoveryServiceAvailable(this._native, service); }
  getReceivers(service) { return requireNative().discoveryGetProviders(this._native, service); }
  setSockOpt(role, option, value) {
    const b = Buffer.isBuffer(value) ? value : Buffer.from(value);
    requireNative().discoverySetSockOpt(this._native, role, option, b);
  }
  close() { if (!this._native) return; requireNative().discoveryDestroy(this._native); this._native = null; }
}

class Gateway {
  constructor(ctx, discovery, routingId = null) {
    this._native = requireNative().gatewayNew(ctx._native, discovery._native, routingId);
  }
  send(service, parts, flags = 0) { requireNative().gatewaySend(this._native, service, parts, flags); }
  recv(flags = 0) { return requireNative().gatewayRecv(this._native, flags); }
  setLoadBalancing(service, strategy) { requireNative().gatewaySetLbStrategy(this._native, service, strategy); }
  setTlsClient(ca, host, trust) { requireNative().gatewaySetTlsClient(this._native, ca, host, trust); }
  connectionCount(service) { return requireNative().gatewayConnectionCount(this._native, service); }
  setSockOpt(option, value) {
    const b = Buffer.isBuffer(value) ? value : Buffer.from(value);
    requireNative().gatewaySetSockOpt(this._native, option, b);
  }
  close() { if (!this._native) return; requireNative().gatewayDestroy(this._native); this._native = null; }
}

class Receiver {
  constructor(ctx, routingId = null) { this._native = requireNative().providerNew(ctx._native, routingId); }
  bind(endpoint) { requireNative().providerBind(this._native, endpoint); }
  connectRegistry(endpoint) { requireNative().providerConnectRegistry(this._native, endpoint); }
  register(service, endpoint, weight) { requireNative().providerRegister(this._native, service, endpoint, weight); }
  updateWeight(service, weight) { requireNative().providerUpdateWeight(this._native, service, weight); }
  unregister(service) { requireNative().providerUnregister(this._native, service); }
  registerResult(service) { return requireNative().providerRegisterResult(this._native, service); }
  setTlsServer(cert, key) { requireNative().providerSetTlsServer(this._native, cert, key); }
  setSockOpt(role, option, value) {
    const b = Buffer.isBuffer(value) ? value : Buffer.from(value);
    requireNative().providerSetSockOpt(this._native, role, option, b);
  }
  routerSocket() {
    const h = requireNative().providerRouter(this._native);
    const s = Object.create(Socket.prototype);
    s._native = h;
    s._own = false;
    return s;
  }
  close() { if (!this._native) return; requireNative().providerDestroy(this._native); this._native = null; }
}

class SpotNode {
  constructor(ctx) { this._native = requireNative().spotNodeNew(ctx._native); }
  bind(endpoint) { requireNative().spotNodeBind(this._native, endpoint); }
  connectRegistry(endpoint) { requireNative().spotNodeConnectRegistry(this._native, endpoint); }
  connectPeerPub(endpoint) { requireNative().spotNodeConnectPeerPub(this._native, endpoint); }
  disconnectPeerPub(endpoint) { requireNative().spotNodeDisconnectPeerPub(this._native, endpoint); }
  register(service, endpoint) { requireNative().spotNodeRegister(this._native, service, endpoint); }
  unregister(service) { requireNative().spotNodeUnregister(this._native, service); }
  setDiscovery(discovery, service) { requireNative().spotNodeSetDiscovery(this._native, discovery._native, service); }
  setTlsServer(cert, key) { requireNative().spotNodeSetTlsServer(this._native, cert, key); }
  setTlsClient(ca, host, trust) { requireNative().spotNodeSetTlsClient(this._native, ca, host, trust); }
  pubSocket() {
    const h = requireNative().spotNodePubSocket(this._native);
    const s = Object.create(Socket.prototype);
    s._native = h;
    s._own = false;
    return s;
  }
  subSocket() {
    const h = requireNative().spotNodeSubSocket(this._native);
    const s = Object.create(Socket.prototype);
    s._native = h;
    s._own = false;
    return s;
  }
  close() { if (!this._native) return; requireNative().spotNodeDestroy(this._native); this._native = null; }
}

class Spot {
  constructor(node) { this._native = requireNative().spotNew(node._native); }
  publish(topic, parts, flags = 0) { requireNative().spotPublish(this._native, topic, parts, flags); }
  subscribe(topic) { requireNative().spotSubscribe(this._native, topic); }
  subscribePattern(pattern) { requireNative().spotSubscribePattern(this._native, pattern); }
  unsubscribe(topicOrPattern) { requireNative().spotUnsubscribe(this._native, topicOrPattern); }
  recv(flags = 0) { return requireNative().spotRecv(this._native, flags); }
  pubSocket() {
    const h = requireNative().spotPubSocket(this._native);
    const s = Object.create(Socket.prototype);
    s._native = h;
    s._own = false;
    return s;
  }
  subSocket() {
    const h = requireNative().spotSubSocket(this._native);
    const s = Object.create(Socket.prototype);
    s._native = h;
    s._own = false;
    return s;
  }
  close() { if (!this._native) return; requireNative().spotDestroy(this._native); this._native = null; }
}

function version() { return requireNative().version(); }

module.exports = {
  version,
  // Backward-compatible constant aliases
  SERVICE_TYPE_GATEWAY: ServiceType.GATEWAY,
  SERVICE_TYPE_SPOT: ServiceType.SPOT,
  SocketType,
  ContextOption,
  SocketOption,
  SendFlag,
  ReceiveFlag,
  MonitorEvent,
  DisconnectReason,
  PollEvent,
  ServiceType,
  GatewayLbStrategy,
  RegistrySocketRole,
  DiscoverySocketRole,
  GatewaySocketRole,
  ReceiverSocketRole,
  SpotNodeSocketRole,
  SpotSocketRole,
  Context,
  Socket,
  MonitorSocket,
  Poller,
  Registry,
  Discovery,
  Gateway,
  Receiver,
  SpotNode,
  Spot
};
