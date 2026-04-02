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
import { validateCString } from './validation';
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
export type SocketSendReadyHandler = () => void;
export type SpotSubHandler = SocketSubscribeHandler;
export type SpotSendReadyHandler = () => void;

const ROUTING_ID_MAX_LENGTH = 255;

function boolAsUint32Buffer(value: boolean): Buffer {
  const buffer = Buffer.allocUnsafe(4);
  buffer.writeUInt32LE(value ? 1 : 0, 0);
  return buffer;
}

function int32Buffer(value: number, name: string): Buffer {
  if (!Number.isInteger(value)) {
    throw new TypeError(`${name} must be an integer`);
  }
  if (value < -2147483648 || value > 2147483647) {
    throw new RangeError(`${name} must fit in int32`);
  }
  const buffer = Buffer.allocUnsafe(4);
  buffer.writeInt32LE(value, 0);
  return buffer;
}

function int64Buffer(value: number | bigint, name: string): Buffer {
  let normalized: bigint;
  if (typeof value === 'bigint') {
    normalized = value;
  } else {
    if (!Number.isInteger(value)) {
      throw new TypeError(`${name} must be an integer`);
    }
    if (!Number.isSafeInteger(value)) {
      throw new RangeError(`${name} must be a safe integer when passed as a number`);
    }
    normalized = BigInt(value);
  }
  const min = -(1n << 63n);
  const max = (1n << 63n) - 1n;
  if (normalized < min || normalized > max) {
    throw new RangeError(`${name} must fit in int64`);
  }
  const buffer = Buffer.allocUnsafe(8);
  buffer.writeBigInt64LE(normalized, 0);
  return buffer;
}

function readBoolOption(buffer: Buffer, name: string): boolean {
  if (buffer.length < 4) {
    throw new Error(`${name} option returned an invalid payload`);
  }
  return buffer.readUInt32LE(0) !== 0;
}

function readInt32Option(buffer: Buffer, name: string): number {
  if (buffer.length < 4) {
    throw new Error(`${name} option returned an invalid payload`);
  }
  return buffer.readInt32LE(0);
}

function readUint32Option(buffer: Buffer, name: string): number {
  if (buffer.length < 4) {
    throw new Error(`${name} option returned an invalid payload`);
  }
  return buffer.readUInt32LE(0);
}

function readInt64Option(buffer: Buffer, name: string): bigint {
  if (buffer.length < 8) {
    throw new Error(`${name} option returned an invalid payload`);
  }
  return buffer.readBigInt64LE(0);
}

function normalizeRoutingId(routingId: BufferLike): Buffer {
  const normalized = normalizeBufferLike(routingId, 'routingId');
  if (normalized.length > ROUTING_ID_MAX_LENGTH) {
    throw new RangeError(`routingId must be at most ${ROUTING_ID_MAX_LENGTH} bytes`);
  }
  return normalized;
}

export class CommonSocketOptions {
  protected readonly _socket: BaseSocket;

  constructor(socket: BaseSocket) {
    this._socket = socket;
  }

  get linger(): number {
    return readInt32Option(this._socket.getOptionRawInternal(SocketOption.LINGER), 'linger');
  }

  set linger(value: number) {
    this._socket.setOptionRawInternal(SocketOption.LINGER, int32Buffer(value, 'linger'));
  }

  get sendHwm(): number {
    return readInt32Option(this._socket.getOptionRawInternal(SocketOption.SNDHWM), 'sendHwm');
  }

  set sendHwm(value: number) {
    this._socket.setOptionRawInternal(SocketOption.SNDHWM, int32Buffer(value, 'sendHwm'));
  }

  get recvHwm(): number {
    return readInt32Option(this._socket.getOptionRawInternal(SocketOption.RCVHWM), 'recvHwm');
  }

  set recvHwm(value: number) {
    this._socket.setOptionRawInternal(SocketOption.RCVHWM, int32Buffer(value, 'recvHwm'));
  }

  get sendTimeout(): number {
    return readInt32Option(this._socket.getOptionRawInternal(SocketOption.SNDTIMEO), 'sendTimeout');
  }

  set sendTimeout(value: number) {
    this._socket.setOptionRawInternal(SocketOption.SNDTIMEO, int32Buffer(value, 'sendTimeout'));
  }

  get recvTimeout(): number {
    return readInt32Option(this._socket.getOptionRawInternal(SocketOption.RCVTIMEO), 'recvTimeout');
  }

  set recvTimeout(value: number) {
    this._socket.setOptionRawInternal(SocketOption.RCVTIMEO, int32Buffer(value, 'recvTimeout'));
  }

  get immediate(): boolean {
    return readBoolOption(this._socket.getOptionRawInternal(SocketOption.IMMEDIATE), 'immediate');
  }

  set immediate(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.IMMEDIATE, boolAsUint32Buffer(value));
  }

  get connectTimeout(): number {
    return readInt32Option(
      this._socket.getOptionRawInternal(SocketOption.CONNECT_TIMEOUT),
      'connectTimeout'
    );
  }

  set connectTimeout(value: number) {
    this._socket.setOptionRawInternal(
      SocketOption.CONNECT_TIMEOUT,
      int32Buffer(value, 'connectTimeout')
    );
  }

  get ipv6(): boolean {
    return readBoolOption(this._socket.getOptionRawInternal(SocketOption.IPV6), 'ipv6');
  }

  set ipv6(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.IPV6, boolAsUint32Buffer(value));
  }

  get tcpNoDelay(): boolean {
    return readBoolOption(this._socket.getOptionRawInternal(SocketOption.TCP_NODELAY), 'tcpNoDelay');
  }

  set tcpNoDelay(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.TCP_NODELAY, boolAsUint32Buffer(value));
  }

  get tcpKeepalive(): number {
    return readInt32Option(
      this._socket.getOptionRawInternal(SocketOption.TCP_KEEPALIVE),
      'tcpKeepalive'
    );
  }

  set tcpKeepalive(value: number) {
    this._socket.setOptionRawInternal(
      SocketOption.TCP_KEEPALIVE,
      int32Buffer(value, 'tcpKeepalive')
    );
  }

  get heartbeatInterval(): number {
    return readInt32Option(
      this._socket.getOptionRawInternal(SocketOption.HEARTBEAT_IVL),
      'heartbeatInterval'
    );
  }

  set heartbeatInterval(value: number) {
    this._socket.setOptionRawInternal(
      SocketOption.HEARTBEAT_IVL,
      int32Buffer(value, 'heartbeatInterval')
    );
  }

  get heartbeatTtl(): number {
    return readInt32Option(
      this._socket.getOptionRawInternal(SocketOption.HEARTBEAT_TTL),
      'heartbeatTtl'
    );
  }

  set heartbeatTtl(value: number) {
    this._socket.setOptionRawInternal(
      SocketOption.HEARTBEAT_TTL,
      int32Buffer(value, 'heartbeatTtl')
    );
  }

  get heartbeatTimeout(): number {
    return readInt32Option(
      this._socket.getOptionRawInternal(SocketOption.HEARTBEAT_TIMEOUT),
      'heartbeatTimeout'
    );
  }

  set heartbeatTimeout(value: number) {
    this._socket.setOptionRawInternal(
      SocketOption.HEARTBEAT_TIMEOUT,
      int32Buffer(value, 'heartbeatTimeout')
    );
  }

  get maxMsgSize(): bigint {
    return readInt64Option(
      this._socket.getOptionRawInternal(SocketOption.MAXMSGSIZE),
      'maxMsgSize'
    );
  }

  set maxMsgSize(value: number | bigint) {
    this._socket.setOptionRawInternal(SocketOption.MAXMSGSIZE, int64Buffer(value, 'maxMsgSize'));
  }

  get backlog(): number {
    return readInt32Option(this._socket.getOptionRawInternal(SocketOption.BACKLOG), 'backlog');
  }

  set backlog(value: number) {
    this._socket.setOptionRawInternal(SocketOption.BACKLOG, int32Buffer(value, 'backlog'));
  }

  get reconnectInterval(): number {
    return readInt32Option(
      this._socket.getOptionRawInternal(SocketOption.RECONNECT_IVL),
      'reconnectInterval'
    );
  }

  set reconnectInterval(value: number) {
    this._socket.setOptionRawInternal(
      SocketOption.RECONNECT_IVL,
      int32Buffer(value, 'reconnectInterval')
    );
  }

  get reconnectIntervalMax(): number {
    return readInt32Option(
      this._socket.getOptionRawInternal(SocketOption.RECONNECT_IVL_MAX),
      'reconnectIntervalMax'
    );
  }

  set reconnectIntervalMax(value: number) {
    this._socket.setOptionRawInternal(
      SocketOption.RECONNECT_IVL_MAX,
      int32Buffer(value, 'reconnectIntervalMax')
    );
  }
}

export class DealerSocketOptions extends CommonSocketOptions {
  constructor(socket: BaseSocket) {
    super(socket);
  }

  set probe(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.DEALER_PROBE, boolAsUint32Buffer(value));
  }
}

export class RouterSocketOptions extends CommonSocketOptions {
  constructor(socket: BaseSocket) {
    super(socket);
  }

  get mandatory(): boolean {
    return readBoolOption(
      this._socket.getOptionRawInternal(SocketOption.ROUTER_MANDATORY),
      'mandatory'
    );
  }

  set mandatory(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.ROUTER_MANDATORY, boolAsUint32Buffer(value));
  }

  get handover(): boolean {
    return readBoolOption(
      this._socket.getOptionRawInternal(SocketOption.ROUTER_HANDOVER),
      'handover'
    );
  }

  set handover(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.ROUTER_HANDOVER, boolAsUint32Buffer(value));
  }

  get probe(): boolean {
    return readBoolOption(this._socket.getOptionRawInternal(SocketOption.PROBE_ROUTER), 'probe');
  }

  set probe(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.PROBE_ROUTER, boolAsUint32Buffer(value));
  }

  set connectRoutingId(value: BufferLike) {
    this._socket.setOptionRawInternal(
      SocketOption.CONNECT_ROUTING_ID,
      normalizeRoutingId(value)
    );
  }
}

export class StreamSocketOptions extends CommonSocketOptions {
  constructor(socket: BaseSocket) {
    super(socket);
  }

  get notify(): boolean {
    return readBoolOption(this._socket.getOptionRawInternal(SocketOption.STREAM_NOTIFY), 'notify');
  }

  set notify(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.STREAM_NOTIFY, boolAsUint32Buffer(value));
  }
}

export class PubSocketOptions extends CommonSocketOptions {
  constructor(socket: BaseSocket) {
    super(socket);
  }

  set verbose(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.XPUB_VERBOSE, boolAsUint32Buffer(value));
  }

  set verboser(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.XPUB_VERBOSER, boolAsUint32Buffer(value));
  }

  set noDrop(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.XPUB_NODROP, boolAsUint32Buffer(value));
  }

  set manual(value: boolean) {
    this._socket.setOptionRawInternal(SocketOption.XPUB_MANUAL, boolAsUint32Buffer(value));
  }
}

export class SubSocketOptions extends CommonSocketOptions {
  constructor(socket: BaseSocket) {
    super(socket);
  }

  get topicsCount(): number {
    return readUint32Option(
      this._socket.getOptionRawInternal(SocketOption.SUB_TOPICS_COUNT),
      'topicsCount'
    );
  }
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

function startEventPollingLoop<T extends { value?: number }>(
  isOpen: () => boolean,
  read: () => T | null,
  shouldInvoke: (value: T) => boolean,
  invoke: (value: T) => void
): void {
  startPollingLoop(isOpen, read, (value) => {
    if (shouldInvoke(value)) {
      invoke(value);
    }
  });
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
export const SpotNodeState = Object.freeze({
  IDLE: 1, CONNECTING: 2, PARTIAL_READY: 3, READY: 4, ERROR: 5
} as const);
export const SpotPeerSource = Object.freeze({
  MANUAL: 1, DISCOVERY: 2, MIXED: 3
} as const);
export const SpotPeerState = Object.freeze({
  CONFIGURED: 1, CONNECTING: 2, CONNECTED: 3
} as const);
export const RegistryState = Object.freeze({
  IDLE: 1, ACTIVE: 2, DEGRADED: 3, ERROR: 4
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

export class ServiceEvent {
  readonly serviceKind: number;
  readonly eventType: number;
  readonly status: number;
  readonly errorCode: number;
  readonly value: number;
  readonly detailFlags: number;
  readonly serviceName: string;
  readonly endpoint: string;
  readonly routingId: Buffer | null;
  readonly subject: string;
  readonly subjectKind: number;

  constructor(raw: ServiceEventValue) {
    this.serviceKind = raw.serviceKind;
    this.eventType = raw.eventType;
    this.status = raw.status;
    this.errorCode = raw.errorCode;
    this.value = raw.value;
    this.detailFlags = raw.detailFlags;
    this.serviceName = raw.serviceName;
    this.endpoint = raw.endpoint;
    this.routingId = raw.routingId;
    this.subject = raw.subject;
    this.subjectKind = raw.subjectKind;
  }
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

export interface RegistryServiceSummaryFilter {
  serviceKind?: number;
  serviceRole?: number;
  serviceName?: string;
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

export interface SpotNodePeerFilter {
  peerEndpoint?: string;
  source?: number;
  state?: number;
}

export interface SpotNodeSubjectEntry {
  role: number;
  subject: string;
  subjectKind: number;
  readyPeerCount: number;
  activePeerCount: number;
  lastChangedMs: number;
}

export interface SpotNodeSubjectFilter {
  role?: number;
  subject?: string;
  subjectKind?: number;
}

function sanitizeRegistryTopologyFilter(
  filter?: RegistryTopologyFilter
): RegistryTopologyFilter | undefined {
  if (!filter) {
    return undefined;
  }
  return {
    ...filter,
    serviceName: filter.serviceName === undefined
      ? undefined
      : validateCString(filter.serviceName, 'serviceName')
  };
}

function sanitizeRegistryServiceSummaryFilter(
  filter?: RegistryServiceSummaryFilter
): RegistryServiceSummaryFilter | undefined {
  if (!filter) {
    return undefined;
  }
  return {
    ...filter,
    serviceName: filter.serviceName === undefined
      ? undefined
      : validateCString(filter.serviceName, 'serviceName')
  };
}

function sanitizeSpotNodePeerFilter(
  filter?: SpotNodePeerFilter
): SpotNodePeerFilter | undefined {
  if (!filter) {
    return undefined;
  }
  return {
    ...filter,
    peerEndpoint: filter.peerEndpoint === undefined
      ? undefined
      : validateCString(filter.peerEndpoint, 'peerEndpoint')
  };
}

function sanitizeSpotNodeSubjectFilter(
  filter?: SpotNodeSubjectFilter
): SpotNodeSubjectFilter | undefined {
  if (!filter) {
    return undefined;
  }
  return {
    ...filter,
    subject: filter.subject === undefined
      ? undefined
      : validateCString(filter.subject, 'subject')
  };
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

export abstract class ConnectableSocketBase extends BaseSocket {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  connect(endpoint: string): void {
    requireNative().socketConnect(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }

  disconnect(endpoint: string): void {
    requireNative().socketDisconnect(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }
}

export abstract class ConnectableSendSocketBase extends ConnectableSocketBase {
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

export abstract class PublisherSocketBase extends ConnectableSocketBase {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  publish(topic: string, message: MessageLike): number;
  publish(topic: string, parts: readonly MessageLike[]): number;
  publish(topic: string, payloadOrParts: MessageLike | readonly MessageLike[]): number {
    const normalizedTopic = validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER);
    if (Array.isArray(payloadOrParts)) {
      return requireNative().socketPublish(
        this.nativeHandle(),
        normalizedTopic,
        normalizeMultipart(payloadOrParts),
        0
      ) as number;
    }
    const payload = payloadOrParts as MessageLike;
    return requireNative().socketPublish(
      this.nativeHandle(),
      normalizedTopic,
      normalizeMessagePayload(payload),
      0
    ) as number;
  }

  tryPublish(topic: string, message: MessageLike): SendResult;
  tryPublish(topic: string, parts: readonly MessageLike[]): SendResult;
  tryPublish(topic: string, payloadOrParts: MessageLike | readonly MessageLike[]): SendResult {
    const normalizedTopic = validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER);
    if (Array.isArray(payloadOrParts)) {
      return requireNative().socketTryPublish(
        this.nativeHandle(),
        normalizedTopic,
        normalizeMultipart(payloadOrParts)
      ) as SendResult;
    }
    const payload = payloadOrParts as MessageLike;
    return requireNative().socketTryPublish(
      this.nativeHandle(),
      normalizedTopic,
      normalizeMessagePayload(payload)
    ) as SendResult;
  }
}

export abstract class MessageSocketBase extends ConnectableSendSocketBase {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  recv(): Received {
    return materializeReceived(
      requireNative().socketRecvMessage(this.nativeHandle(), 0) as {
        parts: Buffer[];
        routingId?: Buffer | null;
      }
    );
  }

  tryRecv(): Received | null {
    const raw = requireNative().socketTryRecvMessage(this.nativeHandle()) as
      | { parts: Buffer[]; routingId?: Buffer | null }
      | null;
    return raw ? materializeReceived(raw) : null;
  }

  onReceive(handler: SocketRecvHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    startPollingLoop(
      () => this.nativeHandle() != null,
      () => this.tryRecv(),
      (received) => handler(received.routingId, [...received.parts])
    );
  }

  onSendReady(handler: SocketSendReadyHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    const monitor = this.monitorOpen(MonitorEvent.CONNECTION_READY_CHANGED);
    startEventPollingLoop(
      () => this.nativeHandle() != null,
      () => monitor.tryRecv(),
      (event) => event.event === MonitorEvent.CONNECTION_READY_CHANGED && event.value > 0,
      () => handler()
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

  recv(): Received {
    return materializeReceived(
      requireNative().socketRecvMessage(this.nativeHandle(), 0) as {
        parts: Buffer[];
        routingId?: Buffer | null;
      }
    );
  }

  tryRecv(): Received | null {
    const raw = requireNative().socketTryRecvMessage(this.nativeHandle()) as
      | { parts: Buffer[]; routingId?: Buffer | null }
      | null;
    return raw ? materializeReceived(raw) : null;
  }

  onReceive(handler: SocketRecvHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    startPollingLoop(
      () => this.nativeHandle() != null,
      () => this.tryRecv(),
      (received) => handler(received.routingId, [...received.parts])
    );
  }

  onSendReady(handler: SocketSendReadyHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    const monitor = this.monitorOpen(MonitorEvent.CONNECTION_READY_CHANGED);
    startEventPollingLoop(
      () => this.nativeHandle() != null,
      () => monitor.tryRecv(),
      (event) => event.event === MonitorEvent.CONNECTION_READY_CHANGED && event.value > 0,
      () => handler()
    );
  }
}

export abstract class ConnectableRoutedMessageSocketBase extends RoutedMessageSocketBase {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  connect(endpoint: string): void {
    requireNative().socketConnect(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }

  disconnect(endpoint: string): void {
    requireNative().socketDisconnect(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }
}

export abstract class SubscriberSocketBase extends ConnectableSocketBase {
  protected constructor(ctx: Context, type: number) {
    super(ctx, type);
  }

  setSubscription(topicOrPattern: string): void {
    this.setSockOptRaw(
      SocketOption.SUBSCRIBE,
      validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER)
    );
  }

  unsetSubscription(topicOrPattern: string): void {
    this.setSockOptRaw(
      SocketOption.UNSUBSCRIBE,
      validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER)
    );
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

  onSubscribe(handler: SocketSubscribeHandler): void {
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
  readonly options: PubSocketOptions;

  constructor(ctx: Context) {
    super(ctx, SocketType.PUB);
    this.options = new PubSocketOptions(this);
  }

  onSendReady(handler: SocketSendReadyHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    const monitor = this.monitorOpen(MonitorEvent.PUB_DELIVERY_READY_CHANGED);
    startEventPollingLoop(
      () => this.nativeHandle() != null,
      () => monitor.tryRecv(),
      (event) => event.event === MonitorEvent.PUB_DELIVERY_READY_CHANGED && event.value > 0,
      () => handler()
    );
  }

  attachDiscovery(discovery: Discovery): void {
    requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
  }
}

export class XPubSocket extends PublisherSocketBase {
  readonly options: PubSocketOptions;

  constructor(ctx: Context) {
    super(ctx, SocketType.XPUB);
    this.options = new PubSocketOptions(this);
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

  onSendReady(handler: SocketSendReadyHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    const monitor = this.monitorOpen(MonitorEvent.PUB_DELIVERY_READY_CHANGED);
    startEventPollingLoop(
      () => this.nativeHandle() != null,
      () => monitor.tryRecv(),
      (event) => event.event === MonitorEvent.PUB_DELIVERY_READY_CHANGED && event.value > 0,
      () => handler()
    );
  }
}

export class PairSocket extends MessageSocketBase {
  readonly options: CommonSocketOptions;

  constructor(ctx: Context) {
    super(ctx, SocketType.PAIR);
    this.options = new CommonSocketOptions(this);
  }
}

export class DealerSocket extends MessageSocketBase {
  readonly options: DealerSocketOptions;

  constructor(ctx: Context) {
    super(ctx, SocketType.DEALER);
    this.options = new DealerSocketOptions(this);
  }

  setRoutingId(routingId: BufferLike): void {
    this.setSockOptRaw(SocketOption.ROUTING_ID, normalizeRoutingId(routingId));
  }

  getRoutingId(): Buffer {
    return this.getSockOptRaw(SocketOption.ROUTING_ID);
  }

  attachDiscovery(discovery: Discovery): void {
    requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
  }
}

export class RouterSocket extends ConnectableRoutedMessageSocketBase {
  readonly options: RouterSocketOptions;

  constructor(ctx: Context) {
    super(ctx, SocketType.ROUTER);
    this.options = new RouterSocketOptions(this);
  }

  setRoutingId(routingId: BufferLike): void {
    this.setSockOptRaw(SocketOption.ROUTING_ID, normalizeRoutingId(routingId));
  }

  getRoutingId(): Buffer {
    return this.getSockOptRaw(SocketOption.ROUTING_ID);
  }

  attachDiscovery(discovery: Discovery): void {
    requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
  }
}

export class StreamSocket extends RoutedMessageSocketBase {
  readonly options: StreamSocketOptions;

  constructor(ctx: Context) {
    super(ctx, SocketType.STREAM);
    this.options = new StreamSocketOptions(this);
  }

  setRoutingId(routingId: BufferLike): void {
    this.setSockOptRaw(SocketOption.ROUTING_ID, normalizeRoutingId(routingId));
  }

  getRoutingId(): Buffer {
    return this.getSockOptRaw(SocketOption.ROUTING_ID);
  }
}

export class SubSocket extends SubscriberSocketBase {
  readonly options: SubSocketOptions;

  constructor(ctx: Context) {
    super(ctx, SocketType.SUB);
    this.options = new SubSocketOptions(this);
  }

  attachDiscovery(discovery: Discovery): void {
    requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
  }
}

export class XSubSocket extends SubscriberSocketBase {
  readonly options: SubSocketOptions;

  constructor(ctx: Context) {
    super(ctx, SocketType.XSUB);
    this.options = new SubSocketOptions(this);
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

  recv(): ServiceEvent {
    return new ServiceEvent(requireNative().serviceMonitorRecv(this._native) as ServiceEventValue);
  }

  tryRecv(): ServiceEvent | null {
    const raw = requireNative().serviceMonitorTryRecv(this._native) as ServiceEventValue | null;
    return raw ? new ServiceEvent(raw) : null;
  }

  snapshot(): MonitorSnapshot {
    return requireNative().monitorSnapshot(this._native) as MonitorSnapshot;
  }

  onEvent(handler: (event: ServiceEvent) => void): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    startPollingLoop(
      () => this._native != null,
      () => this.tryRecv(),
      (event) => handler(event)
    );
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
    requireNative().registrySetEndpoints(
      this._native,
      validateCString(pub, 'pubEndpoint'),
      validateCString(router, 'routerEndpoint')
    );
    this._bound = true;
  }

  setId(id: number): void {
    requireNative().registrySetId(this._native, id | 0);
  }

  addPeer(pub: string): void {
    requireNative().registryAddPeer(this._native, validateCString(pub, 'pubEndpoint'));
  }

  setHeartbeat(intervalMs: number, timeoutMs: number): void {
    requireNative().registrySetHeartbeat(this._native, intervalMs | 0, timeoutMs | 0);
  }

  setBroadcastInterval(intervalMs: number): void {
    requireNative().registrySetBroadcastInterval(this._native, intervalMs | 0);
  }

  statusSnapshot(): RegistryStatus {
    return requireNative().registryStatusSnapshot(this._native) as RegistryStatus;
  }

  serviceSummarySnapshot(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[] {
    return requireNative().registryServiceSummarySnapshot(
      this._native,
      sanitizeRegistryServiceSummaryFilter(filter)
    ) as RegistryServiceSummaryEntry[];
  }

  topologySnapshot(): RegistryTopologyEntry[] {
    return requireNative().registryTopologySnapshot(this._native) as RegistryTopologyEntry[];
  }

  topologyQuery(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    return requireNative().registryTopologyQuery(
      this._native,
      sanitizeRegistryTopologyFilter(filter)
    ) as RegistryTopologyEntry[];
  }

  memberPeers(serviceType: number, serviceName = ''): MemberPeerEntry[] {
    return requireNative().registryMemberPeers(
      this._native,
      serviceType,
      validateCString(serviceName, 'serviceName')
    ) as MemberPeerEntry[];
  }

  memberPeerMetadata(
    serviceType: number,
    serviceName: string,
    serviceRole: number,
    endpoint: string
  ): Buffer {
    return requireNative().registryMemberPeerMetadata(
      this._native,
      serviceType,
      validateCString(serviceName, 'serviceName'),
      serviceRole,
      validateCString(endpoint, 'endpoint')
    ) as Buffer;
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
    requireNative().registryQueryClientConnect(this._native, validateCString(endpoint, 'endpoint'));
  }

  snapshot(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    return requireNative().registryQuerySnapshot(
      this._native,
      sanitizeRegistryTopologyFilter(filter)
    ) as RegistryTopologyEntry[];
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
    validateCString(serviceName, 'serviceName');
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
    requireNative().discoveryConnectRegistry(this._native, validateCString(endpoint, 'endpoint'));
  }

  setValue(value: number): void {
    requireNative().discoverySetValue(this._native, value);
  }

  getValue(): number {
    return requireNative().discoveryGetValue(this._native) as number;
  }

  setMetadata(metadata: BufferLike | string): void {
    requireNative().discoverySetMetadata(
      this._native,
      normalizeBufferLike(metadata, 'metadata')
    );
  }

  getMetadata(): Buffer {
    return requireNative().discoveryGetMetadata(this._native) as Buffer;
  }

  memberPeers(): MemberPeerEntry[] {
    return requireNative().discoveryGetProviders(this._native) as MemberPeerEntry[];
  }

  memberPeerMetadata(serviceRole: number, endpoint: string): Buffer {
    return requireNative().discoveryMemberPeerMetadata(
      this._native,
      serviceRole,
      validateCString(endpoint, 'endpoint')
    ) as Buffer;
  }

  monitorOpen(
    events = ServiceMonitorEvent.ERROR | ServiceMonitorEvent.CLOSED
  ): ServiceMonitor {
    return new ServiceMonitor(requireNative().discoveryOpenMonitor(this._native, events | 0));
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
    requireNative().spotNodeBind(this._native, validateCString(endpoint, 'endpoint'));
  }

  connectPeer(endpoint: string): void {
    requireNative().spotNodeConnectPeerPub(this._native, validateCString(endpoint, 'endpoint'));
  }

  disconnectPeer(endpoint: string): void {
    requireNative().spotNodeDisconnectPeerPub(this._native, validateCString(endpoint, 'endpoint'));
  }

  attachDiscovery(discovery: Discovery): void {
    requireNative().spotNodeSetDiscovery(this._native, discovery.nativeHandle());
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

  peersQuery(filter?: SpotNodePeerFilter): SpotNodePeerEntry[] {
    return requireNative().spotNodePeersQuery(
      this._native,
      sanitizeSpotNodePeerFilter(filter)
    ) as SpotNodePeerEntry[];
  }

  subjectsSnapshot(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[] {
    return requireNative().spotNodeSubjectsSnapshot(
      this._native,
      sanitizeSpotNodeSubjectFilter(filter)
    ) as SpotNodeSubjectEntry[];
  }

  monitorOpen(
    events = ServiceMonitorEvent.ERROR
      | ServiceMonitorEvent.CLOSED
      | ServiceMonitorEvent.SPOT_SUB_DELIVERY_READY_CHANGED
  ): ServiceMonitor {
    return new ServiceMonitor(requireNative().spotNodeOpenMonitor(this._native, events | 0));
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

  private setSocketOption(option: number, value: Buffer): void {
    requireNative().socketSetOpt(this._native, option | 0, value);
  }

  publish(topic: string, payload: MessageLike): void;
  publish(topic: string, payloadParts: readonly MessageLike[]): void;
  publish(topic: string, payloadOrParts: MessageLike | readonly MessageLike[]): void {
    const normalizedTopic = validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER);
    if (Array.isArray(payloadOrParts)) {
      requireNative().spotPublish(
        this._native,
        normalizedTopic,
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
    requireNative().spotPublish(this._native, normalizedTopic, payload, 0);
  }

  tryPublish(topic: string, payload: MessageLike): SendResult;
  tryPublish(topic: string, payloadParts: readonly MessageLike[]): SendResult;
  tryPublish(topic: string, payloadOrParts: MessageLike | readonly MessageLike[]): SendResult {
    const normalizedTopic = validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER);
    if (Array.isArray(payloadOrParts)) {
      return requireNative().spotTryPublish(
        this._native,
        normalizedTopic,
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
    return requireNative().spotTryPublish(this._native, normalizedTopic, payload) as SendResult;
  }

  setLinger(milliseconds: number): void {
    this.setSocketOption(SocketOption.LINGER, int32Buffer(milliseconds, 'milliseconds'));
  }

  setSendHighWaterMark(value: number): void {
    this.setSocketOption(SocketOption.SNDHWM, int32Buffer(value, 'value'));
  }

  setReceiveHighWaterMark(value: number): void {
    this.setSocketOption(SocketOption.RCVHWM, int32Buffer(value, 'value'));
  }

  setSendTimeout(milliseconds: number): void {
    this.setSocketOption(SocketOption.SNDTIMEO, int32Buffer(milliseconds, 'milliseconds'));
  }

  setReceiveTimeout(milliseconds: number): void {
    this.setSocketOption(SocketOption.RCVTIMEO, int32Buffer(milliseconds, 'milliseconds'));
  }

  setNoDrop(enabled: boolean): void {
    this.setSocketOption(SocketOption.XPUB_NODROP, boolAsUint32Buffer(enabled));
  }

  setSubscription(topicOrPattern: string): void {
    requireNative().spotSubscribe(
      this._native,
      validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER)
    );
  }

  unsetSubscription(topicOrPattern: string): void {
    requireNative().spotUnsubscribe(
      this._native,
      validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER)
    );
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

  onSubscribe(handler: SpotSubHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    startPollingLoop(
      () => this._native != null,
      () => this.trySubscribe(),
      (received) => handler(received.routingId, received.topic, [...received.parts])
    );
  }

  onSendReady(handler: SpotSendReadyHandler): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    const monitor = this.monitorOpen(
      ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED
        | ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED
    );
    startEventPollingLoop(
      () => this._native != null,
      () => monitor.tryRecv(),
      (event) => (
        (event.eventType === ServiceMonitorEvent.SPOT_PUB_DELIVERY_READY_CHANGED
          || event.eventType === ServiceMonitorEvent.SPOT_FIRST_DELIVERY_READY_CHANGED)
        && event.value > 0
      ),
      () => handler()
    );
  }

  monitorOpen(
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
