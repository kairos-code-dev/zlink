'use strict';

function loadNative() {
  try {
    if (process.platform === 'linux') {
      const path = require('path');
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
      const path = require('path');
      const prebuilt = path.join(__dirname, '..', 'prebuilds', `${process.platform}-${process.arch}`, 'zlink.node');
      return require(prebuilt);
    } catch (err2) {
      return null;
    }
  }
}

const native = loadNative();
const SERVICE_TYPE_GATEWAY_RECEIVER = 1;
const SERVICE_TYPE_SPOT_NODE = 2;

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
  topicCreate(topic, mode) { requireNative().spotTopicCreate(this._native, topic, mode); }
  topicDestroy(topic) { requireNative().spotTopicDestroy(this._native, topic); }
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
  SERVICE_TYPE_GATEWAY_RECEIVER,
  SERVICE_TYPE_SPOT_NODE,
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
