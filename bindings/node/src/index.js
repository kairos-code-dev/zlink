// SPDX-License-Identifier: MPL-2.0

'use strict';

const { requireNative } = require('./native');
const { normalizeBufferLike } = require('./buffer_like');
const { Message, Received } = require('./message');
const {
  SocketType,
  SocketOption,
  SendFlag,
  ReceiveFlag,
  StreamDispatchMode
} = require('./socket/constants');
const { BaseSocket } = require('./socket/base_socket');
const { SendSocket } = require('./socket/send_socket');
const { DuplexSocket } = require('./socket/duplex_socket');
const { SubscriberSocket } = require('./socket/subscriber_socket');
const { Socket } = require('./socket/compat_socket');
const { MonitorSocket } = require('./socket/monitor_socket');
const {
  PubSocket,
  XPubSocket,
  PairSocket,
  DealerSocket,
  RouterSocket,
  StreamSocket,
  SubSocket,
  XSubSocket
} = require('./socket/socket_types');

const ContextOption = Object.freeze({
  IO_THREADS: 1, MAX_SOCKETS: 2, SOCKET_LIMIT: 3,
  THREAD_PRIORITY: 3, THREAD_SCHED_POLICY: 4, MAX_MSGSZ: 5,
  MSG_T_SIZE: 6, THREAD_AFFINITY_CPU_ADD: 7,
  THREAD_AFFINITY_CPU_REMOVE: 8, THREAD_NAME_PREFIX: 9,
  BLOCKY: 10
});

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
  BaseSocket,
  SendSocket,
  DuplexSocket,
  SubscriberSocket,
  Socket,
  MonitorSocket,
  PubSocket,
  XPubSocket,
  PairSocket,
  DealerSocket,
  RouterSocket,
  StreamSocket,
  SubSocket,
  XSubSocket,
  ServiceMonitor,
  Poller,
  Registry,
  RegistryQueryClient,
  Discovery,
  SpotNode,
  Spot
};
