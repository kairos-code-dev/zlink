// SPDX-License-Identifier: MPL-2.0

import { requireNative } from './native';
import { normalizeBufferLike } from './buffer_like';
import { Message, Received, Subscribed, SubscriptionEvent } from './message';
import {
  SocketType,
  SocketOption
} from './socket/constants';
import { BaseSocket } from './socket/base_socket';
import { Socket } from './socket/compat_socket';
import { MonitorSocket } from './socket/monitor_socket';
import {
  materializeReceived,
  materializeSubscribed,
  materializeSubscriptionEvent,
  normalizeMessagePayload,
  normalizeMultipart
} from './socket/socket_support';
import type { BufferLike } from './buffer_like';
import type { MessageLike } from './message';

export type { BufferLike, MessageLike };
export {
  Message,
  Received,
  Subscribed,
  SubscriptionEvent,
  BaseSocket,
  Socket,
  MonitorSocket,
  SocketType,
  SocketOption
};

export const SendResult = Object.freeze({
  Sent: 0,
  Backpressured: 1,
  NotReady: 2
} as const);

export type SendResult = typeof SendResult[keyof typeof SendResult];
export type SocketRecvHandler = (routingId: Buffer | null, parts: Message[]) => void;
export type SocketSubscribeHandler = (
  routingId: Buffer | null,
  topic: string,
  parts: Message[]
) => void;
export type SpotSubHandler = SocketSubscribeHandler;

const ROUTING_ID_MAX_LENGTH = 255;

function boolAsUint32Buffer(value: boolean): Buffer {
  const buffer = Buffer.allocUnsafe(4);
  buffer.writeUInt32LE(value ? 1 : 0, 0);
  return buffer;
}

function normalizeRoutingId(routingId: BufferLike): Buffer {
  const normalized = normalizeBufferLike(routingId, 'routingId');
  if (normalized.length > ROUTING_ID_MAX_LENGTH) {
    throw new RangeError(`routingId must be at most ${ROUTING_ID_MAX_LENGTH} bytes`);
  }
  return normalized;
}

function startPollingLoop<T>(
  isOpen: () => boolean,
  read: () => T | null,
  invoke: (value: T) => void
): void {
  const tick = (): void => {
    if (!isOpen()) {
      return;
    }
    try {
      const value = read();
      if (value !== null) {
        invoke(value);
      }
    } catch (error) {
      if (!isOpen()) {
        return;
      }
      setImmediate(() => {
        throw error;
      });
      return;
    }
    setImmediate(tick);
  };
  setImmediate(tick);
}

export const ContextOption = Object.freeze({
  IO_THREADS: 1, MAX_SOCKETS: 2, SOCKET_LIMIT: 3,
  THREAD_PRIORITY: 3, THREAD_SCHED_POLICY: 4, MAX_MSGSZ: 5,
  MSG_T_SIZE: 6, THREAD_AFFINITY_CPU_ADD: 7,
  THREAD_AFFINITY_CPU_REMOVE: 8, THREAD_NAME_PREFIX: 9,
  BLOCKY: 10
} as const);

export const ErrorCode = Object.freeze({
  EFSM: 156384763, ENOCOMPATPROTO: 156384764, ETERM: 156384765, EMTHREAD: 156384766
} as const);

export const ProtocolError = Object.freeze({
  ZMP_MALFORMED_COMMAND_HELLO: 0x10000013
} as const);

export const MonitorEvent = Object.freeze({
  CONNECTED: 0x0001, CONNECT_DELAYED: 0x0002, CONNECT_RETRIED: 0x0004,
  LISTENING: 0x0008, BIND_FAILED: 0x0010, ACCEPTED: 0x0020,
  ACCEPT_FAILED: 0x0040, CLOSED: 0x0080, CLOSE_FAILED: 0x0100,
  DISCONNECTED: 0x0200, MONITOR_STOPPED: 0x0400,
  HANDSHAKE_FAILED_NO_DETAIL: 0x0800,
  CONNECTION_READY_CHANGED: 0x1000, HANDSHAKE_FAILED_PROTOCOL: 0x2000,
  HANDSHAKE_FAILED_AUTH: 0x4000,
  SUB_DELIVERY_READY_CHANGED: 0x8000, PUB_DELIVERY_READY_CHANGED: 0x10000,
  ALL: 0xFFFF
} as const);

export const DisconnectReason = Object.freeze({
  UNKNOWN: 0, HANDSHAKE_FAILED: 3, TRANSPORT_ERROR: 4, CTX_TERM: 5
} as const);

export const PollEvent = Object.freeze({
  POLLIN: 1, POLLOUT: 2, POLLERR: 4, POLLPRI: 8
} as const);

export const ServiceType = Object.freeze({
  SPOT: 0x3002, SOCKET: 0x3003
} as const);

export const SERVICE_TYPE_SPOT = ServiceType.SPOT;
export const SERVICE_TYPE_SOCKET = ServiceType.SOCKET;

export const ServiceRole = Object.freeze({
  INVALID: 0, SPOT: 2, ROUTER: 3, DEALER: 4, PUB: 5, SUB: 6
} as const);

export const ServiceKind = Object.freeze({
  DISCOVERY: 1, SPOT_SUB: 3, SPOT_PUB: 4, SOCKET: 5
} as const);

export const RegistrySocketRole = Object.freeze({ PUB: 5, ROUTER: 3, PEER_SUB: 6 } as const);
export const DiscoverySocketRole = Object.freeze({ SUB: 6 } as const);
export const SpotNodeSocketRole = Object.freeze({ NODE: 0, PUB: 1, SUB: 2, DEALER: 3 } as const);
export const SpotNodeOption = Object.freeze({
  PUB_MODE: 1, PUB_QUEUE_HWM: 2, PUB_QUEUE_FULL_POLICY: 3
} as const);
export const SpotNodePubMode = Object.freeze({ SYNC: 0, ASYNC: 1 } as const);
export const SpotNodePubQueueFullPolicy = Object.freeze({ EAGAIN: 0, DROP: 1 } as const);
export const SpotSocketRole = Object.freeze({ PUB: 1, SUB: 2 } as const);
export const MonitorSourceKind = Object.freeze({ SOCKET: 1, SPOT_PUB: 3, SPOT_SUB: 4 } as const);
export const MonitorState = Object.freeze({
  READY: 1 << 0, BOUND_READY: 1 << 1, SEND_READY: 1 << 2, CLOSED: 1 << 3
} as const);
export const MonitorSnapshotDetail = Object.freeze({
  READY_COUNT: 1 << 0, SND_PENDING_MSGS: 1 << 1, RCV_PENDING_MSGS: 1 << 2
} as const);
export const ServiceMonitorEvent = Object.freeze({
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
} as const);
export const TopologySource = Object.freeze({ MANUAL: 1, DISCOVERY: 2, REGISTRY: 3 } as const);
export const TopologyState = Object.freeze({
  DISCOVERED: 1, CONNECTING: 2, READY: 3, LOST: 4, ERROR: 5, STOPPED: 6
} as const);

export interface MonitorSnapshot {
  sourceKind: number;
  stateFlags: number;
  detailFlags: number;
  readyCount: number;
  sndPendingMsgs: number;
  rcvPendingMsgs: number;
}

export interface SocketMonitorEventValue {
  event: number;
  value: number;
  local: string;
  remote: string;
}

export interface ServiceEventValue {
  serviceKind: number;
  eventType: number;
  status: number;
  errorCode: number;
  value: number;
  detailFlags: number;
  serviceName: string;
  endpoint: string;
  routingId: Buffer | null;
  subject: string;
  subjectKind: number;
}

export interface MemberPeerEntry {
  serviceType: number;
  serviceRole: number;
  serviceName: string;
  endpoint: string;
  routingId: Buffer | null;
  value: number;
}

export interface RegistryStatus {
  registryId: number;
  bindEndpoint: string;
  state: number;
  topologyEntryCount: number;
  peerRegistryCount: number;
  connectedPeerRegistryCount: number;
  listSeq: number;
  lastError: number;
  lastChangedMs: number;
}

export interface RegistryTopologyEntry {
  routingId: Buffer | null;
  serviceKind: number;
  serviceRole: number;
  serviceName: string;
  endpoint: string;
  source: number;
  state: number;
  desiredCount: number;
  readyCount: number;
  errorCode: number;
  lastReportedMs: number;
}

export interface RegistryServiceSummaryEntry {
  serviceKind: number;
  serviceRole: number;
  serviceName: string;
  totalCount: number;
  connectingCount: number;
  readyCount: number;
  errorCount: number;
  stoppedCount: number;
  lastReportedMs: number;
}

export interface RegistryTopologyFilter {
  serviceKind?: number;
  serviceRole?: number;
  serviceName?: string;
  source?: number;
  state?: number;
}

export interface SpotNodeStatus {
  serviceName: string;
  localEndpoint: string;
  nodeRoutingId: Buffer | null;
  state: number;
  configuredPeerCount: number;
  activePeerCount: number;
  connectedPeerCount: number;
  subjectCount: number;
  readySubjectCount: number;
  lastError: number;
  lastChangedMs: number;
}

export interface SpotNodePeerEntry {
  serviceName: string;
  localEndpoint: string;
  peerEndpoint: string;
  source: number;
  state: number;
  connectedSinceMs: number;
  lastChangedMs: number;
}

export interface SpotNodeSubjectEntry {
  role: number;
  subject: string;
  subjectKind: number;
  readyPeerCount: number;
  activePeerCount: number;
  lastChangedMs: number;
}

export abstract class SendSocketBase extends BaseSocket {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  send(message: MessageLike): number;
  send(parts: readonly MessageLike[]): number;
  send(payloadOrParts: MessageLike | readonly MessageLike[]): number {
    if (Array.isArray(payloadOrParts)) {
      return requireNative().socketSendParts(
        this.nativeHandle(),
        normalizeMultipart(payloadOrParts),
        0
      ) as number;
    }
    const payload = payloadOrParts as MessageLike;
    return requireNative().socketSend(
      this.nativeHandle(),
      normalizeMessagePayload(payload),
      0
    ) as number;
  }

  trySend(message: MessageLike): SendResult;
  trySend(parts: readonly MessageLike[]): SendResult;
  trySend(payloadOrParts: MessageLike | readonly MessageLike[]): SendResult {
    if (Array.isArray(payloadOrParts)) {
      return requireNative().socketTrySendParts(
        this.nativeHandle(),
        normalizeMultipart(payloadOrParts)
      ) as SendResult;
    }
    const payload = payloadOrParts as MessageLike;
    return requireNative().socketTrySend(
      this.nativeHandle(),
      normalizeMessagePayload(payload)
    ) as SendResult;
  }
}

export abstract class PublisherSocketBase extends BaseSocket {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  publish(topic: string, message: MessageLike): number;
  publish(topic: string, parts: readonly MessageLike[]): number;
  publish(topic: string, payloadOrParts: MessageLike | readonly MessageLike[]): number {
    if (Array.isArray(payloadOrParts)) {
      return requireNative().socketPublish(
        this.nativeHandle(),
        topic,
        normalizeMultipart(payloadOrParts),
        0
      ) as number;
    }
    const payload = payloadOrParts as MessageLike;
    return requireNative().socketPublish(
      this.nativeHandle(),
      topic,
      normalizeMessagePayload(payload),
      0
    ) as number;
  }

  tryPublish(topic: string, message: MessageLike): SendResult;
  tryPublish(topic: string, parts: readonly MessageLike[]): SendResult;
  tryPublish(topic: string, payloadOrParts: MessageLike | readonly MessageLike[]): SendResult {
    if (Array.isArray(payloadOrParts)) {
      return requireNative().socketTryPublish(
        this.nativeHandle(),
        topic,
        normalizeMultipart(payloadOrParts)
      ) as SendResult;
    }
    const payload = payloadOrParts as MessageLike;
    return requireNative().socketTryPublish(
      this.nativeHandle(),
      topic,
      normalizeMessagePayload(payload)
    ) as SendResult;
  }
}

export abstract class MessageSocketBase extends SendSocketBase {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  receive(): Received {
    return materializeReceived(
      requireNative().socketRecvMessage(this.nativeHandle(), 0) as {
        parts: Buffer[];
        routingId?: Buffer | null;
      }
    );
  }

  tryReceive(): Received | null {
    const raw = requireNative().socketTryRecvMessage(this.nativeHandle()) as
      | { parts: Buffer[]; routingId?: Buffer | null }
      | null;
    return raw ? materializeReceived(raw) : null;
  }

  recvHandler(handler: SocketRecvHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    startPollingLoop(
      () => this.nativeHandle() != null,
      () => this.tryReceive(),
      (received) => handler(received.routingId, [...received.parts])
    );
  }
}

export abstract class RoutedMessageSocketBase extends BaseSocket {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  send(routingId: BufferLike, message: MessageLike): number;
  send(routingId: BufferLike, parts: readonly MessageLike[]): number;
  send(routingId: BufferLike, payloadOrParts: MessageLike | readonly MessageLike[]): number {
    const routing = normalizeRoutingId(routingId);
    if (Array.isArray(payloadOrParts)) {
      return requireNative().socketSendParts(
        this.nativeHandle(),
        [routing, ...normalizeMultipart(payloadOrParts)],
        0
      ) as number;
    }
    const payload = payloadOrParts as MessageLike;
    return requireNative().socketSendParts(
      this.nativeHandle(),
      [routing, normalizeMessagePayload(payload)],
      0
    ) as number;
  }

  trySend(routingId: BufferLike, message: MessageLike): SendResult;
  trySend(routingId: BufferLike, parts: readonly MessageLike[]): SendResult;
  trySend(
    routingId: BufferLike,
    payloadOrParts: MessageLike | readonly MessageLike[]
  ): SendResult {
    const routing = normalizeRoutingId(routingId);
    if (Array.isArray(payloadOrParts)) {
      return requireNative().socketTrySendRoutingParts(
        this.nativeHandle(),
        routing,
        normalizeMultipart(payloadOrParts)
      ) as SendResult;
    }
    const payload = payloadOrParts as MessageLike;
    return requireNative().socketTrySendRouting(
      this.nativeHandle(),
      routing,
      normalizeMessagePayload(payload)
    ) as SendResult;
  }

  receive(): Received {
    return materializeReceived(
      requireNative().socketRecvMessage(this.nativeHandle(), 0) as {
        parts: Buffer[];
        routingId?: Buffer | null;
      }
    );
  }

  tryReceive(): Received | null {
    const raw = requireNative().socketTryRecvMessage(this.nativeHandle()) as
      | { parts: Buffer[]; routingId?: Buffer | null }
      | null;
    return raw ? materializeReceived(raw) : null;
  }

  recvHandler(handler: SocketRecvHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    startPollingLoop(
      () => this.nativeHandle() != null,
      () => this.tryReceive(),
      (received) => handler(received.routingId, [...received.parts])
    );
  }
}

export abstract class SubscriberSocketBase extends BaseSocket {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  setSubscription(topicOrPattern: string): void {
    this.setSockOptRaw(SocketOption.SUBSCRIBE, topicOrPattern);
  }

  unsetSubscription(topicOrPattern: string): void {
    this.setSockOptRaw(SocketOption.UNSUBSCRIBE, topicOrPattern);
  }

  subscribe(): Subscribed {
    return materializeSubscribed(
      requireNative().socketSubscribeMessage(this.nativeHandle()) as {
        parts: Buffer[];
        routingId?: Buffer | null;
        topic: string;
      }
    );
  }

  trySubscribe(): Subscribed | null {
    const raw = requireNative().socketTrySubscribeMessage(this.nativeHandle()) as
      | { parts: Buffer[]; routingId?: Buffer | null; topic: string }
      | null;
    return raw ? materializeSubscribed(raw) : null;
  }

  subscribeHandler(handler: SocketSubscribeHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    startPollingLoop(
      () => this.nativeHandle() != null,
      () => this.trySubscribe(),
      (received) => handler(received.routingId, received.topic, [...received.parts])
    );
  }
}

export class PubSocket extends PublisherSocketBase {
  constructor(ctx: Context) {
    super(ctx, SocketType.PUB);
  }
}

export class XPubSocket extends PublisherSocketBase {
  constructor(ctx: Context) {
    super(ctx, SocketType.XPUB);
  }

  receiveSubscriptionEvent(): SubscriptionEvent {
    return materializeSubscriptionEvent(
      requireNative().socketSubscriptionEvent(this.nativeHandle()) as {
        routingId?: Buffer | null;
        topic: string;
        subscribed: boolean;
      }
    );
  }

  tryReceiveSubscriptionEvent(): SubscriptionEvent | null {
    const raw = requireNative().socketTrySubscriptionEvent(this.nativeHandle()) as
      | { routingId?: Buffer | null; topic: string; subscribed: boolean }
      | null;
    return raw ? materializeSubscriptionEvent(raw) : null;
  }

  setVerbose(enabled: boolean): void {
    this.setSockOptRaw(SocketOption.XPUB_VERBOSE, boolAsUint32Buffer(enabled));
  }

  setVerboser(enabled: boolean): void {
    this.setSockOptRaw(SocketOption.XPUB_VERBOSER, boolAsUint32Buffer(enabled));
  }

  setNoDrop(enabled: boolean): void {
    this.setSockOptRaw(SocketOption.XPUB_NODROP, boolAsUint32Buffer(enabled));
  }
}

export class PairSocket extends MessageSocketBase {
  constructor(ctx: Context) {
    super(ctx, SocketType.PAIR);
  }
}

export class DealerSocket extends MessageSocketBase {
  constructor(ctx: Context) {
    super(ctx, SocketType.DEALER);
  }

  setRoutingId(routingId: BufferLike): void {
    this.setSockOptRaw(SocketOption.ROUTING_ID, normalizeRoutingId(routingId));
  }

  getRoutingId(): Buffer {
    return this.getSockOptRaw(SocketOption.ROUTING_ID);
  }
}

export class RouterSocket extends RoutedMessageSocketBase {
  constructor(ctx: Context) {
    super(ctx, SocketType.ROUTER);
  }

  setRoutingId(routingId: BufferLike): void {
    this.setSockOptRaw(SocketOption.ROUTING_ID, normalizeRoutingId(routingId));
  }

  getRoutingId(): Buffer {
    return this.getSockOptRaw(SocketOption.ROUTING_ID);
  }
}

export class StreamSocket extends RoutedMessageSocketBase {
  constructor(ctx: Context) {
    super(ctx, SocketType.STREAM);
  }
}

export class SubSocket extends SubscriberSocketBase {
  constructor(ctx: Context) {
    super(ctx, SocketType.SUB);
  }
}

export class XSubSocket extends SubscriberSocketBase {
  constructor(ctx: Context) {
    super(ctx, SocketType.XSUB);
  }
}

export class Context {
  /** @internal */
  private _native: unknown | null;

  constructor() {
    this._native = requireNative().ctxNew();
  }

  /** @internal */
  nativeHandle(): unknown {
    return this._native;
  }

  close(): void {
    if (!this._native) return;
    requireNative().ctxTerm(this._native);
    this._native = null;
  }
}

export class ServiceMonitor {
  /** @internal */
  private _native: unknown | null;

  constructor(handle: unknown) {
    this._native = handle;
  }

  recv(): ServiceEventValue {
    return requireNative().serviceMonitorRecv(this._native) as ServiceEventValue;
  }

  tryRecv(): ServiceEventValue | null {
    return requireNative().serviceMonitorTryRecv(this._native) as ServiceEventValue | null;
  }

  snapshot(): MonitorSnapshot {
    return requireNative().monitorSnapshot(this._native) as MonitorSnapshot;
  }

  close(): void {
    if (!this._native) return;
    requireNative().monitorClose(this._native);
    this._native = null;
  }
}

export class Poller {
  private readonly _items: Array<{ socket: unknown; fd: number; events: number }>;

  constructor() {
    this._items = [];
  }

  addSocket(socket: BaseSocket, events: number): void {
    this._items.push({ socket: socket.nativeHandle(), fd: 0, events: events | 0 });
  }

  poll(timeoutMs: number): number[] {
    return requireNative().poll(this._items, timeoutMs | 0) as number[];
  }
}

export class Registry {
  /** @internal */
  private _native: unknown | null;
  private _bound: boolean;

  constructor(ctx: Context) {
    this._native = requireNative().registryNew(ctx.nativeHandle());
    this._bound = false;
  }

  /** @internal */
  nativeHandle(): unknown {
    return this._native;
  }

  bind(pub: string, router: string): void {
    if (this._bound) {
      throw new Error('Registry.bind may only be called once on the aligned public API');
    }
    requireNative().registrySetEndpoints(this._native, pub, router);
    this._bound = true;
  }

  setId(id: number): void {
    requireNative().registrySetId(this._native, id | 0);
  }

  addPeer(pub: string): void {
    requireNative().registryAddPeer(this._native, pub);
  }

  setHeartbeat(intervalMs: number, timeoutMs: number): void {
    requireNative().registrySetHeartbeat(this._native, intervalMs | 0, timeoutMs | 0);
  }

  setBroadcastInterval(intervalMs: number): void {
    requireNative().registrySetBroadcastInterval(this._native, intervalMs | 0);
  }

  setEndpoints(): void {
    throw new Error('Registry.setEndpoints is removed from the aligned public API. Use Registry.bind(pub, router).');
  }

  start(): void {
    throw new Error('Registry.start is removed from the aligned public API. Use Registry.bind(pub, router).');
  }

  statusSnapshot(): RegistryStatus {
    return requireNative().registryStatusSnapshot(this._native) as RegistryStatus;
  }

  serviceSummarySnapshot(): RegistryServiceSummaryEntry[] {
    return requireNative().registryServiceSummarySnapshot(this._native) as RegistryServiceSummaryEntry[];
  }

  topologySnapshot(): RegistryTopologyEntry[] {
    return requireNative().registryTopologySnapshot(this._native) as RegistryTopologyEntry[];
  }

  topologyQuery(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    return requireNative().registryTopologyQuery(this._native, filter) as RegistryTopologyEntry[];
  }

  memberPeers(serviceType: number, serviceName = ''): MemberPeerEntry[] {
    return requireNative().registryMemberPeers(
      this._native,
      serviceType,
      serviceName
    ) as MemberPeerEntry[];
  }

  setSockOpt(): void {
    throw new Error('Registry.setSockOpt is not available on the aligned public API');
  }

  close(): void {
    if (!this._native) return;
    requireNative().registryDestroy(this._native);
    this._native = null;
  }
}

export class RegistryQueryClient {
  /** @internal */
  private _native: unknown | null;

  constructor(ctx: Context) {
    this._native = requireNative().registryQueryClientNew(ctx.nativeHandle());
  }

  connect(endpoint: string): void {
    requireNative().registryQueryClientConnect(this._native, endpoint);
  }

  snapshot(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    return requireNative().registryQuerySnapshot(this._native, filter) as RegistryTopologyEntry[];
  }

  close(): void {
    if (!this._native) return;
    requireNative().registryQueryDestroy(this._native);
    this._native = null;
  }
}

export class Discovery {
  /** @internal */
  private _native: unknown | null;
  readonly serviceType: number;
  readonly serviceName: string;

  constructor(ctx: Context, serviceType: number, serviceName: string) {
    if (typeof serviceName !== 'string' || serviceName.length === 0) {
      throw new TypeError('Discovery serviceName must be a non-empty string');
    }
    this._native = requireNative().discoveryNew(
      ctx.nativeHandle(),
      serviceType,
      serviceName
    );
    this.serviceType = serviceType;
    this.serviceName = serviceName;
  }

  /** @internal */
  nativeHandle(): unknown {
    return this._native;
  }

  connectRegistry(endpoint: string): void {
    requireNative().discoveryConnectRegistry(this._native, endpoint);
  }

  receiverCount(): number {
    return requireNative().discoveryProviderCount(this._native) as number;
  }

  setValue(value: number): void {
    requireNative().discoverySetValue(this._native, value);
  }

  value(): number {
    return requireNative().discoveryGetValue(this._native) as number;
  }

  setMetadata(metadata: BufferLike | string): void {
    requireNative().discoverySetMetadata(
      this._native,
      normalizeBufferLike(metadata, 'metadata')
    );
  }

  metadata(): Buffer {
    return requireNative().discoveryGetMetadata(this._native) as Buffer;
  }

  memberPeers(): MemberPeerEntry[] {
    return requireNative().discoveryGetProviders(this._native) as MemberPeerEntry[];
  }

  serviceAvailable(): boolean {
    return requireNative().discoveryServiceAvailable(this._native) as boolean;
  }

  openMonitor(
    events = ServiceMonitorEvent.ERROR | ServiceMonitorEvent.CLOSED
  ): ServiceMonitor {
    return new ServiceMonitor(requireNative().discoveryOpenMonitor(this._native, events | 0));
  }

  setTlsClient(caCert: string, hostname: string, trustSystem = 0): void {
    requireNative().discoverySetTlsClient(
      this._native,
      caCert || '',
      hostname || '',
      trustSystem | 0
    );
  }

  close(): void {
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

void Receiver;

export class SpotNode {
  /** @internal */
  private _native: unknown | null;

  constructor(ctx: Context) {
    this._native = requireNative().spotNodeNew(ctx.nativeHandle());
  }

  nativeHandle(): unknown {
    return this._native;
  }

  bind(endpoint: string): void {
    requireNative().spotNodeBind(this._native, endpoint);
  }

  connectPeerPub(endpoint: string): void {
    requireNative().spotNodeConnectPeerPub(this._native, endpoint);
  }

  disconnectPeerPub(endpoint: string): void {
    requireNative().spotNodeDisconnectPeerPub(this._native, endpoint);
  }

  attachDiscovery(discovery: Discovery): void {
    requireNative().spotNodeSetDiscovery(this._native, discovery.nativeHandle());
  }

  setDiscovery(_: Discovery): void {
    throw new Error('SpotNode.setDiscovery is removed from the aligned public API. Use attachDiscovery(discovery).');
  }

  register(): void {
    throw new Error('SpotNode.register is removed from the aligned public API. Attach a Discovery instead.');
  }

  unregister(): void {
    throw new Error('SpotNode.unregister is removed from the aligned public API.');
  }

  setTlsServer(cert: string, key: string, requireClient = 0): void {
    requireNative().spotNodeSetTlsServer(this._native, cert, key, requireClient | 0);
  }

  setTlsClient(ca: string, host: string, trust = 0): void {
    requireNative().spotNodeSetTlsClient(this._native, ca, host, trust | 0);
  }

  statusSnapshot(): SpotNodeStatus {
    return requireNative().spotNodeStatusSnapshot(this._native) as SpotNodeStatus;
  }

  peersSnapshot(): SpotNodePeerEntry[] {
    return requireNative().spotNodePeersSnapshot(this._native) as SpotNodePeerEntry[];
  }

  subjectsSnapshot(): SpotNodeSubjectEntry[] {
    return requireNative().spotNodeSubjectsSnapshot(this._native) as SpotNodeSubjectEntry[];
  }

  openMonitor(
    events = ServiceMonitorEvent.ERROR
      | ServiceMonitorEvent.CLOSED
      | ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED
  ): ServiceMonitor {
    return new ServiceMonitor(requireNative().spotNodeOpenMonitor(this._native, events | 0));
  }

  setSockOpt(): void {
    throw new Error('SpotNode.setSockOpt is not available on the aligned public API');
  }

  pubSocket(): void {
    throw new Error('SpotNode.pubSocket is not available on the aligned public API');
  }

  subSocket(): void {
    throw new Error('SpotNode.subSocket is not available on the aligned public API');
  }

  pubPeers(): void {
    throw new Error('SpotNode.pubPeers is not available on the aligned public API');
  }

  subPeers(): void {
    throw new Error('SpotNode.subPeers is not available on the aligned public API');
  }

  close(): void {
    if (!this._native) return;
    requireNative().spotNodeDestroy(this._native);
    this._native = null;
  }
}

export class Spot {
  /** @internal */
  private _native: unknown | null;

  constructor(node: SpotNode) {
    this._native = requireNative().spotNew(node.nativeHandle());
    requireNative().spotEnableSendReadyNoop(this._native);
  }

  publish(topic: string, payload: MessageLike): void;
  publish(topic: string, payloadParts: readonly MessageLike[]): void;
  publish(topic: string, payloadOrParts: MessageLike | readonly MessageLike[]): void {
    if (Array.isArray(payloadOrParts)) {
      requireNative().spotPublish(
        this._native,
        topic,
        payloadOrParts.map((part, index) => {
          return part instanceof Message
            ? part.payloadBuffer()
            : normalizeBufferLike(part, `payloadOrParts[${index}]`);
        }),
        0
      );
      return;
    }
    const payloadValue = payloadOrParts as MessageLike;
    const payload = payloadValue instanceof Message
      ? payloadValue.payloadBuffer()
      : normalizeBufferLike(payloadValue, 'payload');
    requireNative().spotPublish(this._native, topic, payload, 0);
  }

  tryPublish(topic: string, payload: MessageLike): SendResult;
  tryPublish(topic: string, payloadParts: readonly MessageLike[]): SendResult;
  tryPublish(topic: string, payloadOrParts: MessageLike | readonly MessageLike[]): SendResult {
    if (Array.isArray(payloadOrParts)) {
      return requireNative().spotTryPublish(
        this._native,
        topic,
        payloadOrParts.map((part, index) => {
          return part instanceof Message
            ? part.payloadBuffer()
            : normalizeBufferLike(part, `payloadOrParts[${index}]`);
        })
      ) as SendResult;
    }
    const payloadValue = payloadOrParts as MessageLike;
    const payload = payloadValue instanceof Message
      ? payloadValue.payloadBuffer()
      : normalizeBufferLike(payloadValue, 'payload');
    return requireNative().spotTryPublish(this._native, topic, payload) as SendResult;
  }

  setSubscription(topicOrPattern: string): void {
    requireNative().spotSubscribe(this._native, topicOrPattern);
  }

  unsetSubscription(topicOrPattern: string): void {
    requireNative().spotUnsubscribe(this._native, topicOrPattern);
  }

  subscribe(): Subscribed {
    return materializeSubscribed(
      requireNative().spotRecv(this._native) as {
        routingId?: Buffer | null;
        topic: string;
        parts: Buffer[];
      }
    );
  }

  trySubscribe(): Subscribed | null {
    const raw = requireNative().spotTryRecv(this._native) as
      | { routingId?: Buffer | null; topic: string; parts: Buffer[] }
      | null;
    return raw ? materializeSubscribed(raw) : null;
  }

  subscribeHandler(handler: SpotSubHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    startPollingLoop(
      () => this._native != null,
      () => this.trySubscribe(),
      (received) => handler(received.routingId, received.topic, [...received.parts])
    );
  }

  openMonitor(
    events = ServiceMonitorEvent.ERROR | ServiceMonitorEvent.CLOSED
  ): ServiceMonitor {
    return new ServiceMonitor(requireNative().spotOpenMonitor(this._native, events | 0));
  }

  close(): void {
    if (!this._native) return;
    requireNative().spotDestroy(this._native);
    this._native = null;
  }
}

export function version(): [number, number, number] {
  return requireNative().version() as [number, number, number];
}
