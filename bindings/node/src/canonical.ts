// SPDX-License-Identifier: MPL-2.0

import { randomBytes } from 'node:crypto';
import { requireNative } from './native';
import { normalizeBufferLike, type BufferLike } from './buffer_like';
import {
  Message,
  Received,
  RoutingId,
  TopicMessage,
  SubscriptionEvent,
  type MessageLike,
  type MessageSnapshot
} from './message';
import { validateCString } from './validation';
import { SocketType, SocketOption, SendFlags, RecvFlags } from './socket/constants';
import {
  BindError,
  BindResult,
  CloseError,
  CloseResult,
  ConfigError,
  ConfigResult,
  ConnectError,
  ConnectResult,
  HandlerError,
  HandlerResult,
  RecvError,
  RecvResult,
  RequestError,
  RequestResult,
  SubmitError,
  SubmitResult,
  ZlinkError,
  createError
} from './errors';

export type { BufferLike, MessageLike };
export {
  Message,
  Received,
  RoutingId,
  TopicMessage,
  SubscriptionEvent,
  SendFlags,
  RecvFlags,
  SubmitResult,
  RequestResult,
  RecvResult,
  HandlerResult,
  CloseResult,
  BindResult,
  ConnectResult,
  ConfigResult,
  ZlinkError,
  SubmitError,
  RequestError,
  RecvError,
  HandlerError,
  CloseError,
  BindError,
  ConnectError,
  ConfigError
};

export const AdmissionState = Object.freeze({
  Serving: 1,
  Draining: 2
} as const);
export type AdmissionState = typeof AdmissionState[keyof typeof AdmissionState];

const ContextOption = Object.freeze({
  IO_THREADS: 1,
  MAX_SOCKETS: 2,
  SOCKET_LIMIT: 3,
  THREAD_PRIORITY: 3,
  THREAD_SCHED_POLICY: 4,
  MAX_MSGSZ: 5,
  MSG_T_SIZE: 6,
  THREAD_AFFINITY_CPU_ADD: 7,
  THREAD_AFFINITY_CPU_REMOVE: 8,
  THREAD_NAME_PREFIX: 9,
  BLOCKY: 10
} as const);

const MonitorSourceKind = Object.freeze({ SOCKET: 1, SPOT_PUB: 3, SPOT_SUB: 4 } as const);
const MonitorState = Object.freeze({
  READY: 1 << 0, BOUND_READY: 1 << 1, CLOSED: 1 << 3
} as const);
const MonitorSnapshotDetail = Object.freeze({
  SND_PENDING_MSGS: 1 << 1, RCV_PENDING_MSGS: 1 << 2
} as const);

export type MonitorEventType = number;

export interface MonitorSnapshot {
  readonly sourceKind: number;
  readonly stateFlags: number;
  readonly detailFlags: number;
  readonly sndPendingMsgs: bigint;
  readonly rcvPendingMsgs: bigint;
  isReady(): boolean;
}

interface MonitorSnapshotRaw {
  sourceKind: number;
  stateFlags: number;
  detailFlags: number;
  sndPendingMsgs: number | bigint;
  rcvPendingMsgs: number | bigint;
}

interface MonitorEventValueRaw {
  event: number;
  value: number;
  routingId?: Buffer | null;
  local?: string;
  remote?: string;
  localAddr?: string;
  remoteAddr?: string;
}

export class MonitorEvent {
  static readonly CONNECTED = 0x0001;
  static readonly CONNECT_DELAYED = 0x0002;
  static readonly CONNECT_RETRIED = 0x0004;
  static readonly LISTENING = 0x0008;
  static readonly BIND_FAILED = 0x0010;
  static readonly ACCEPTED = 0x0020;
  static readonly ACCEPT_FAILED = 0x0040;
  static readonly CLOSED = 0x0080;
  static readonly CLOSE_FAILED = 0x0100;
  static readonly DISCONNECTED = 0x0200;
  static readonly MONITOR_STOPPED = 0x0400;
  static readonly HANDSHAKE_FAILED_NO_DETAIL = 0x0800;
  static readonly CONNECTION_READY = 0x1000;
  static readonly HANDSHAKE_FAILED_PROTOCOL = 0x2000;
  static readonly HANDSHAKE_FAILED_AUTH = 0x4000;
  static readonly PEER_ADMISSION_CHANGED = 0x8000;
  static readonly ALL = 0xFFFF;

  readonly event: MonitorEventType;
  readonly value: number;
  readonly routingId: RoutingId | null;
  readonly localAddr: string;
  readonly remoteAddr: string;

  constructor(raw: MonitorEventValueRaw) {
    this.event = raw.event;
    this.value = raw.value;
    this.routingId = wrapRoutingId(raw.routingId ?? null);
    this.localAddr = raw.localAddr ?? raw.local ?? '';
    this.remoteAddr = raw.remoteAddr ?? raw.remote ?? '';
  }
}

Object.freeze(MonitorEvent);

export const ServiceMonitorEventMask = Object.freeze({
  error: 1 << 4,
  discoveryServiceUp: 1 << 5,
  discoveryServiceDown: 1 << 6,
  discoveryProvidersChanged: 1 << 7,
  peerAdmissionChanged: 1 << 8,
  closed: 1 << 17,
  all: (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 17)
} as const);
export type ServiceMonitorEventMask = number;

const ServiceType = Object.freeze({ SPOT: 0x3002, SOCKET: 0x3003 } as const);
const SERVICE_TYPE_SPOT = ServiceType.SPOT;
const SERVICE_TYPE_SOCKET = ServiceType.SOCKET;
export enum DiscoveryDealerPeerMode {
  Router = 1,
  Dealer = 2
}
const ServiceRole = Object.freeze({
  INVALID: 0, SPOT: 2, ROUTER: 3, DEALER: 4, PUB: 5, SUB: 6
} as const);
const ServiceKind = Object.freeze({
  DISCOVERY: 1, SPOT_SUB: 3, SPOT_PUB: 4, SOCKET: 5
} as const);
const SpotPeerSource = Object.freeze({ MANUAL: 1, DISCOVERY: 2, MIXED: 3 } as const);
const SpotPeerState = Object.freeze({ CONFIGURED: 1, CONNECTING: 2, CONNECTED: 3 } as const);
const SpotNodeState = Object.freeze({ IDLE: 1, CONNECTING: 2, PARTIAL_READY: 3, READY: 4, ERROR: 5 } as const);
const RegistryState = Object.freeze({ IDLE: 1, ACTIVE: 2, DEGRADED: 3, ERROR: 4 } as const);
const TopologySource = Object.freeze({ MANUAL: 1, DISCOVERY: 2, REGISTRY: 3 } as const);
const TopologyState = Object.freeze({ DISCOVERED: 1, CONNECTING: 2, READY: 3, LOST: 4, ERROR: 5, STOPPED: 6 } as const);

export interface MemberPeerEntry {
  readonly serviceType: number;
  readonly serviceRole: number;
  readonly serviceName: string;
  readonly endpoint: string;
  readonly routingId: RoutingId;
  readonly admissionState: AdmissionState;
  readonly value: bigint;
}

export interface RegistryTopologyEntry {
  readonly routingId: RoutingId;
  readonly serviceKind: number;
  readonly serviceRole: number;
  readonly serviceName: string;
  readonly endpoint: string;
  readonly source: number;
  readonly state: number;
  readonly desiredCount: number;
  readonly readyCount: number;
  readonly errorCode: number;
  readonly lastReportedMs: bigint;
}

export interface RegistryServiceSummaryEntry {
  readonly serviceKind: number;
  readonly serviceRole: number;
  readonly serviceName: string;
  readonly totalCount: number;
  readonly connectingCount: number;
  readonly readyCount: number;
  readonly errorCount: number;
  readonly stoppedCount: number;
  readonly lastReportedMs: bigint;
}

export interface RegistryStatus {
  readonly registryId: number;
  readonly bindEndpoint: string;
  readonly state: number;
  readonly topologyEntryCount: number;
  readonly peerRegistryCount: number;
  readonly connectedPeerRegistryCount: number;
  readonly listSeq: bigint;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

export interface SpotNodeStatus {
  readonly serviceName: string;
  readonly localEndpoint: string;
  readonly nodeRoutingId: RoutingId;
  readonly state: number;
  readonly configuredPeerCount: number;
  readonly activePeerCount: number;
  readonly connectedPeerCount: number;
  readonly subjectCount: number;
  readonly readySubjectCount: number;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

export interface SpotNodePeerEntry {
  readonly serviceName: string;
  readonly localEndpoint: string;
  readonly peerEndpoint: string;
  readonly source: number;
  readonly state: number;
  readonly admissionState: AdmissionState;
  readonly connectedSinceMs: bigint;
  readonly lastChangedMs: bigint;
}

export interface SpotNodeSubjectEntry {
  readonly role: number;
  readonly subject: string;
  readonly subjectKind: number;
  readonly readyPeerCount: number;
  readonly activePeerCount: number;
  readonly lastChangedMs: bigint;
}

const SpotServiceAttachmentRole = Object.freeze({
  ROUTER: 1,
  PUB: 2,
  SUB: 3
} as const);

interface SpotServiceMonitorEvent {
  readonly serviceName: string;
  readonly role: number;
  readonly event: MonitorEvent;
}

export interface RegistryServiceSummaryFilter {
  readonly serviceKind?: number;
  readonly serviceRole?: number;
  readonly serviceName?: string;
}

export interface RegistryTopologyFilter {
  readonly serviceKind?: number;
  readonly serviceRole?: number;
  readonly serviceName?: string;
  readonly routingId?: RoutingId;
  readonly state?: number;
  readonly source?: number;
}

export interface SpotNodePeerFilter {
  readonly peerEndpoint?: string;
  readonly source?: number;
  readonly state?: number;
}

export interface SpotNodeSubjectFilter {
  readonly role?: number;
  readonly subject?: string;
  readonly subjectKind?: number;
}

interface ServiceEventValue {
  serviceKind: number;
  eventType: number;
  status: number;
  errorCode: number;
  value: number | bigint;
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
  readonly value: bigint;
  readonly detailFlags: number;
  readonly serviceName: string;
  readonly endpoint: string;
  readonly routingId: RoutingId | null;
  readonly subject: string;
  readonly subjectKind: number;

  constructor(raw: ServiceEventValue) {
    this.serviceKind = raw.serviceKind;
    this.eventType = raw.eventType;
    this.status = raw.status;
    this.errorCode = raw.errorCode;
    this.value = BigInt(raw.value);
    this.detailFlags = raw.detailFlags;
    this.serviceName = raw.serviceName;
    this.endpoint = raw.endpoint;
    this.routingId = wrapRoutingId(raw.routingId);
    this.subject = raw.subject;
    this.subjectKind = raw.subjectKind;
  }
}

export type SocketRecvHandler = (message: Received) => void;
export type SocketSubscribeHandler = (message: TopicMessage) => void;
export type SocketSendReadyHandler = () => void;
export type StreamPacketHandler = (sourceRid: RoutingId, header: Message, body: Message) => void;
type SocketMonitorHandler = (event: MonitorEvent) => void;
export type SpotSubHandler = SocketSubscribeHandler;
export type SpotSendReadyHandler = () => void;
export type SpotRoutedHandler = (
  sourceRid: RoutingId | null,
  spotRid: RoutingId | null,
  requestSeq: bigint,
  parts: Message[]
) => void;
export interface SpotDispatchInfo {
  readonly event: number;
  readonly subjectKind: number;
  readonly subjectHandle: bigint;
}
export type SpotDispatchEventHandler = (info: SpotDispatchInfo) => void;
export type RequestResultCallback = (result: RequestResult, parts: Message[]) => void;
export type TimerHandler = (timer: Timer, fireCount: bigint) => void;
export type ServiceMonitorHandler = (event: ServiceEvent) => void;

function readErrno(): number {
  const native = requireNative();
  return typeof native.errno === 'function' ? native.errno() as number : 0;
}

function lastError(category:
  | 'submit'
  | 'request'
  | 'recv'
  | 'handler'
  | 'close'
  | 'bind'
  | 'connect'
  | 'config',
message: string): ZlinkError {
  return createError(category, readErrno(), message);
}

function recvNativeError(error: unknown, flags: RecvFlags, fallbackMessage: string): RecvError {
  const message = error instanceof Error && error.message ? error.message : fallbackMessage;
  if ((flags & RecvFlags.DontWait) !== 0 && /Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
    return new RecvError(RecvResult.NoData, readErrno(), message);
  }
  return createError('recv', readErrno(), message) as RecvError;
}

function submitNativeError(error: unknown, flags: SendFlags, fallbackMessage: string): SubmitError {
  const message = error instanceof Error && error.message ? error.message : fallbackMessage;
  if ((flags & SendFlags.DontWait) !== 0 && /Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
    return new SubmitError(SubmitResult.Backpressured, readErrno(), message);
  }
  return createError('submit', readErrno(), message) as SubmitError;
}

function requestErrorFromResult(result: RequestResult, message: string): RequestError {
  return new RequestError(result, 0, message);
}

function submitErrorFromResult(result: SubmitResult, message: string): SubmitError {
  return new SubmitError(result, 0, message);
}

function normalizeReplyFlags(flags: SendFlags = SendFlags.None): SendFlags {
  const normalized = flags | 0;
  if (normalized !== SendFlags.None) {
    throw submitErrorFromResult(
      SubmitResult.NotSupported,
      'reply flags are not supported by the current core library'
    );
  }
  return normalized as SendFlags;
}

function normalizeCallbackFlagsAndTimeout(
  flagsOrTimeout?: SendFlags | number,
  maybeTimeout?: number,
): { flags: SendFlags; timeoutMs: number } {
  if (typeof maybeTimeout === 'number') {
    return {
      flags: (flagsOrTimeout ?? SendFlags.None) as SendFlags,
      timeoutMs: maybeTimeout | 0,
    };
  }
  if (typeof flagsOrTimeout === 'number') {
    if (flagsOrTimeout === SendFlags.None || flagsOrTimeout === SendFlags.DontWait) {
      return { flags: flagsOrTimeout as SendFlags, timeoutMs: 0 };
    }
    return { flags: SendFlags.None, timeoutMs: flagsOrTimeout | 0 };
  }
  return { flags: SendFlags.None, timeoutMs: 0 };
}

function toMessageParts(parts: readonly MessageLike[]): Array<Buffer | MessageSnapshot> {
  return parts.map((part, index) =>
    part instanceof Message ? part.toSnapshot() : normalizeBufferLike(part, `parts[${index}]`)
  );
}

function normalizeMessageLikePayload(message: MessageLike | readonly MessageLike[]): Buffer | MessageSnapshot | Array<Buffer | MessageSnapshot> {
  if (Array.isArray(message)) {
    return toMessageParts(message);
  }
  const scalar = message as MessageLike;
  return scalar instanceof Message ? scalar.toSnapshot() : normalizeBufferLike(scalar, 'message');
}

function wrapRoutingId(routingId: Buffer | Uint8Array | null | undefined): RoutingId | null {
  if (!routingId || routingId.length === 0) {
    return null;
  }
  return RoutingId.fromBytes(routingId);
}

function createRequestKeepAlive(): () => void {
  const handle = setInterval(() => {}, 1000);
  return () => clearInterval(handle);
}

type RequestProgressFn = (handle: unknown) => void;

function startRequestProgress(handle: unknown, pump: RequestProgressFn): () => void {
  const releaseKeepAlive = createRequestKeepAlive();
  const interval = setInterval(() => {
    try {
      pump(handle);
    } catch {
      // Progress calls are best-effort. Completion still arrives through the native callback.
    }
  }, 1);
  return () => {
    clearInterval(interval);
    releaseKeepAlive();
  };
}

function requireRoutingId(routingId: RoutingId, name = 'routingId'): Buffer {
  if (!(routingId instanceof RoutingId)) {
    throw new TypeError(`${name} must be a RoutingId`);
  }
  return routingId.toBytes();
}

function normalizeRoutingId(routingId: RoutingId, name = 'routingId'): Buffer {
  const normalized = requireRoutingId(routingId, name);
  if (normalized.length === 0 || normalized.length > 255) {
    throw new RangeError(`${name} must be 1..255 bytes`);
  }
  return normalized;
}

function materializeMonitorSnapshot(raw: MonitorSnapshotRaw): MonitorSnapshot {
  return {
    sourceKind: raw.sourceKind,
    stateFlags: raw.stateFlags,
    detailFlags: raw.detailFlags,
    sndPendingMsgs: BigInt(raw.sndPendingMsgs),
    rcvPendingMsgs: BigInt(raw.rcvPendingMsgs),
    isReady(): boolean {
      return (this.stateFlags & MonitorState.READY) !== 0;
    }
  };
}

function mapMemberPeerEntry(entry: {
  serviceType: number;
  serviceRole: number;
  serviceName: string;
  endpoint: string;
  routingId: Buffer;
  admissionState: number;
  value: number | bigint;
}): MemberPeerEntry {
  return {
    serviceType: entry.serviceType,
    serviceRole: entry.serviceRole,
    serviceName: entry.serviceName,
    endpoint: entry.endpoint,
    routingId: RoutingId.fromBytes(entry.routingId),
    admissionState: entry.admissionState as AdmissionState,
    value: BigInt(entry.value)
  };
}

function mapRegistryTopologyEntry(entry: {
  routingId: Buffer;
  serviceKind: number;
  serviceRole: number;
  serviceName: string;
  endpoint: string;
  source: number;
  state: number;
  desiredCount: number;
  readyCount: number;
  errorCode: number;
  lastReportedMs: number | bigint;
}): RegistryTopologyEntry {
  return {
    routingId: RoutingId.fromBytes(entry.routingId),
    serviceKind: entry.serviceKind,
    serviceRole: entry.serviceRole,
    serviceName: entry.serviceName,
    endpoint: entry.endpoint,
    source: entry.source,
    state: entry.state,
    desiredCount: entry.desiredCount,
    readyCount: entry.readyCount,
    errorCode: entry.errorCode,
    lastReportedMs: BigInt(entry.lastReportedMs)
  };
}

function mapRegistryStatus(entry: {
  registryId: number;
  bindEndpoint: string;
  state: number;
  topologyEntryCount: number;
  peerRegistryCount: number;
  connectedPeerRegistryCount: number;
  listSeq: number | bigint;
  lastError: number;
  lastChangedMs: number | bigint;
}): RegistryStatus {
  return {
    registryId: entry.registryId,
    bindEndpoint: entry.bindEndpoint,
    state: entry.state,
    topologyEntryCount: entry.topologyEntryCount,
    peerRegistryCount: entry.peerRegistryCount,
    connectedPeerRegistryCount: entry.connectedPeerRegistryCount,
    listSeq: BigInt(entry.listSeq),
    lastError: entry.lastError,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

function mapRegistryServiceSummaryEntry(entry: {
  serviceKind: number;
  serviceRole: number;
  serviceName: string;
  totalCount: number;
  connectingCount: number;
  readyCount: number;
  errorCount: number;
  stoppedCount: number;
  lastReportedMs: number | bigint;
}): RegistryServiceSummaryEntry {
  return {
    serviceKind: entry.serviceKind,
    serviceRole: entry.serviceRole,
    serviceName: entry.serviceName,
    totalCount: entry.totalCount,
    connectingCount: entry.connectingCount,
    readyCount: entry.readyCount,
    errorCount: entry.errorCount,
    stoppedCount: entry.stoppedCount,
    lastReportedMs: BigInt(entry.lastReportedMs)
  };
}

function mapSpotNodeStatus(entry: {
  serviceName: string;
  localEndpoint: string;
  nodeRoutingId?: Buffer | null;
  state: number;
  configuredPeerCount: number;
  activePeerCount: number;
  connectedPeerCount: number;
  subjectCount: number;
  readySubjectCount: number;
  lastError: number;
  lastChangedMs: number | bigint;
}, fallbackRoutingId: RoutingId): SpotNodeStatus {
  const nodeRoutingId = entry.nodeRoutingId
    ? RoutingId.fromBytes(entry.nodeRoutingId)
    : fallbackRoutingId;
  return {
    serviceName: entry.serviceName,
    localEndpoint: entry.localEndpoint,
    nodeRoutingId,
    state: entry.state,
    configuredPeerCount: entry.configuredPeerCount,
    activePeerCount: entry.activePeerCount,
    connectedPeerCount: entry.connectedPeerCount,
    subjectCount: entry.subjectCount,
    readySubjectCount: entry.readySubjectCount,
    lastError: entry.lastError,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

function mapSpotNodePeerEntry(entry: {
  serviceName: string;
  localEndpoint: string;
  peerEndpoint: string;
  source: number;
  state: number;
  admissionState: number;
  connectedSinceMs: number | bigint;
  lastChangedMs: number | bigint;
}): SpotNodePeerEntry {
  return {
    serviceName: entry.serviceName,
    localEndpoint: entry.localEndpoint,
    peerEndpoint: entry.peerEndpoint,
    source: entry.source,
    state: entry.state,
    admissionState: entry.admissionState as AdmissionState,
    connectedSinceMs: BigInt(entry.connectedSinceMs),
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

function mapSpotNodeSubjectEntry(entry: {
  role: number;
  subject: string;
  subjectKind: number;
  readyPeerCount: number;
  activePeerCount: number;
  lastChangedMs: number | bigint;
}): SpotNodeSubjectEntry {
  return {
    role: entry.role,
    subject: entry.subject,
    subjectKind: entry.subjectKind,
    readyPeerCount: entry.readyPeerCount,
    activePeerCount: entry.activePeerCount,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

function mapSpotServiceMonitorEvent(entry: {
  serviceName: string;
  role: number;
  event: MonitorEventValueRaw;
}): SpotServiceMonitorEvent {
  return {
    serviceName: entry.serviceName,
    role: entry.role,
    event: new MonitorEvent(entry.event)
  };
}

function normalizeTopologyFilter(filter?: RegistryTopologyFilter): Record<string, unknown> | undefined {
  if (!filter) {
    return undefined;
  }
  return {
    serviceKind: filter.serviceKind,
    serviceRole: filter.serviceRole,
    serviceName: filter.serviceName,
    routingId: filter.routingId ? normalizeRoutingId(filter.routingId, 'filter.routingId') : undefined,
    state: filter.state,
    source: filter.source
  };
}

function materializeReceived(
  raw: {
    parts: MessageSnapshot[];
    routingId?: Buffer | null;
    requestSeq?: bigint | null;
    spotRid?: Buffer | null;
  },
  reply?: (requestSeq: bigint, parts: readonly Message[], flags: SendFlags) => void
): Received {
  const requestSeq = raw.requestSeq ?? null;
  return new Received(
    raw.parts.map((part) => Message.fromSnapshot(part)),
    wrapRoutingId(raw.routingId ?? null),
    requestSeq,
    wrapRoutingId(raw.spotRid ?? null),
    requestSeq !== null && reply
      ? {
          reply(parts: readonly Message[], flags: SendFlags): void {
            reply(requestSeq, parts, flags);
          }
        }
      : null
  );
}

function materializeTopicMessage(raw: {
  topic: string;
  parts: MessageSnapshot[];
  routingId?: Buffer | null;
  serviceName?: string | null;
}): TopicMessage {
  return new TopicMessage(
    raw.topic,
    raw.parts.map((part) => Message.fromSnapshot(part)),
    wrapRoutingId(raw.routingId ?? null),
    raw.serviceName ?? null
  );
}

class NativeHandle {
  protected _native: unknown | null;

  constructor(native: unknown) {
    this._native = native;
  }

  nativeHandle(): unknown {
    return this._native;
  }

  close(): void {
    this._native = null;
  }
}

class BaseSocket extends NativeHandle {
  constructor(ctx: Context, type: number) {
    super(requireNative().socketNew(ctx.nativeHandle(), type));
    if (!this._native) {
      throw lastError('config', 'socket creation failed');
    }
  }

  bind(endpoint: string): void {
    requireNative().socketBind(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }

  unbind(endpoint: string): void {
    requireNative().socketUnbind(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }

  setTlsServer(cert: string, key: string, requireClient = 0): void {
    requireNative().socketSetTlsServer(
      this.nativeHandle(),
      validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER),
      validateCString(key, 'key', Number.MAX_SAFE_INTEGER),
      requireClient | 0
    );
  }

  setTlsClient(ca: string, host: string, trust = 0): void {
    requireNative().socketSetTlsClient(
      this.nativeHandle(),
      validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER),
      validateCString(host, 'host', Number.MAX_SAFE_INTEGER),
      trust | 0
    );
  }

  setSockOptRaw(option: number, value: Buffer | number): void {
    const buf = typeof value === 'number' ? Buffer.from([value & 0xff, 0, 0, 0]) : value;
    requireNative().socketSetOpt(this.nativeHandle(), option | 0, buf);
  }

  getSockOptRaw(option: number): Buffer {
    const out = requireNative().socketGetOpt(this.nativeHandle(), option | 0) as Buffer;
    return out;
  }

  monitorOpen(events: number = MonitorEvent.ALL): MonitorSocket {
    return new MonitorSocket(requireNative().monitorOpen(this.nativeHandle(), events | 0));
  }

  getAdmissionState(): AdmissionState {
    return requireNative().handleGetAdmissionState(this.nativeHandle()) as AdmissionState;
  }

  setAdmissionState(state: AdmissionState): void {
    requireNative().handleSetAdmissionState(this.nativeHandle(), state | 0);
  }

  setChannelName(channelName: string): void {
    requireNative().socketSetChannelName(
      this.nativeHandle(),
      validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER)
    );
  }

  getChannelName(): string {
    return requireNative().socketGetChannelName(this.nativeHandle()) as string;
  }

  close(): void {
    if (!this._native) return;
    requireNative().socketClose(this._native);
    this._native = null;
  }
}

class ConnectableSocket extends BaseSocket {
  connect(endpoint: string): void {
    requireNative().socketConnect(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }

  disconnect(endpoint: string): void {
    requireNative().socketDisconnect(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }
}

function int32Buffer(value: number, name: string): Buffer {
  if (!Number.isInteger(value)) throw new TypeError(`${name} must be an integer`);
  if (value < -2147483648 || value > 2147483647) {
    throw new RangeError(`${name} must fit in int32`);
  }
  const buf = Buffer.allocUnsafe(4);
  buf.writeInt32LE(value, 0);
  return buf;
}

function int64Buffer(value: number | bigint, name: string): Buffer {
  let normalized: bigint;
  if (typeof value === 'bigint') {
    normalized = value;
  } else {
    if (!Number.isInteger(value)) throw new TypeError(`${name} must be an integer`);
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
  const buf = Buffer.allocUnsafe(8);
  buf.writeBigInt64LE(normalized, 0);
  return buf;
}

function boolBuffer(value: boolean): Buffer {
  const buf = Buffer.allocUnsafe(4);
  buf.writeUInt32LE(value ? 1 : 0, 0);
  return buf;
}

function readBoolOption(buffer: Buffer, name: string): boolean {
  if (buffer.length < 4) throw new Error(`${name} option returned an invalid payload`);
  return buffer.readUInt32LE(0) !== 0;
}

function readInt32Option(buffer: Buffer, name: string): number {
  if (buffer.length < 4) throw new Error(`${name} option returned an invalid payload`);
  return buffer.readInt32LE(0);
}

function readInt64Option(buffer: Buffer, name: string): bigint {
  if (buffer.length < 8) throw new Error(`${name} option returned an invalid payload`);
  return buffer.readBigInt64LE(0);
}

export class CommonSocketOptions {
  protected readonly _socket: BaseSocket;
  constructor(socket: BaseSocket) { this._socket = socket; }
  get linger(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.LINGER), 'linger'); }
  set linger(value: number) { this._socket.setSockOptRaw(SocketOption.LINGER, int32Buffer(value, 'linger')); }
  get sendHwm(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.SNDHWM), 'sendHwm'); }
  set sendHwm(value: number) { this._socket.setSockOptRaw(SocketOption.SNDHWM, int32Buffer(value, 'sendHwm')); }
  get recvHwm(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RCVHWM), 'recvHwm'); }
  set recvHwm(value: number) { this._socket.setSockOptRaw(SocketOption.RCVHWM, int32Buffer(value, 'recvHwm')); }
  get sendTimeout(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.SNDTIMEO), 'sendTimeout'); }
  set sendTimeout(value: number) { this._socket.setSockOptRaw(SocketOption.SNDTIMEO, int32Buffer(value, 'sendTimeout')); }
  get recvTimeout(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RCVTIMEO), 'recvTimeout'); }
  set recvTimeout(value: number) { this._socket.setSockOptRaw(SocketOption.RCVTIMEO, int32Buffer(value, 'recvTimeout')); }
  get immediate(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.IMMEDIATE), 'immediate'); }
  set immediate(value: boolean) { this._socket.setSockOptRaw(SocketOption.IMMEDIATE, boolBuffer(value)); }
  get connectTimeout(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.CONNECT_TIMEOUT), 'connectTimeout'); }
  set connectTimeout(value: number) { this._socket.setSockOptRaw(SocketOption.CONNECT_TIMEOUT, int32Buffer(value, 'connectTimeout')); }
  get ipv6(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.IPV6), 'ipv6'); }
  set ipv6(value: boolean) { this._socket.setSockOptRaw(SocketOption.IPV6, boolBuffer(value)); }
  get tcpNoDelay(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.TCP_NODELAY), 'tcpNoDelay'); }
  set tcpNoDelay(value: boolean) { this._socket.setSockOptRaw(SocketOption.TCP_NODELAY, boolBuffer(value)); }
  get tcpKeepalive(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.TCP_KEEPALIVE), 'tcpKeepalive'); }
  set tcpKeepalive(value: number) { this._socket.setSockOptRaw(SocketOption.TCP_KEEPALIVE, int32Buffer(value, 'tcpKeepalive')); }
  get heartbeatInterval(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.HEARTBEAT_IVL), 'heartbeatInterval'); }
  set heartbeatInterval(value: number) { this._socket.setSockOptRaw(SocketOption.HEARTBEAT_IVL, int32Buffer(value, 'heartbeatInterval')); }
  get heartbeatTtl(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.HEARTBEAT_TTL), 'heartbeatTtl'); }
  set heartbeatTtl(value: number) { this._socket.setSockOptRaw(SocketOption.HEARTBEAT_TTL, int32Buffer(value, 'heartbeatTtl')); }
  get heartbeatTimeout(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.HEARTBEAT_TIMEOUT), 'heartbeatTimeout'); }
  set heartbeatTimeout(value: number) { this._socket.setSockOptRaw(SocketOption.HEARTBEAT_TIMEOUT, int32Buffer(value, 'heartbeatTimeout')); }
  get maxMsgSize(): bigint { return readInt64Option(this._socket.getSockOptRaw(SocketOption.MAXMSGSIZE), 'maxMsgSize'); }
  set maxMsgSize(value: number | bigint) { this._socket.setSockOptRaw(SocketOption.MAXMSGSIZE, int64Buffer(value, 'maxMsgSize')); }
  get backlog(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.BACKLOG), 'backlog'); }
  set backlog(value: number) { this._socket.setSockOptRaw(SocketOption.BACKLOG, int32Buffer(value, 'backlog')); }
  get reconnectInterval(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RECONNECT_IVL), 'reconnectInterval'); }
  set reconnectInterval(value: number) { this._socket.setSockOptRaw(SocketOption.RECONNECT_IVL, int32Buffer(value, 'reconnectInterval')); }
  get reconnectIntervalMax(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RECONNECT_IVL_MAX), 'reconnectIntervalMax'); }
  set reconnectIntervalMax(value: number) { this._socket.setSockOptRaw(SocketOption.RECONNECT_IVL_MAX, int32Buffer(value, 'reconnectIntervalMax')); }
}

export class DealerSocketOptions extends CommonSocketOptions {
  set probe(value: boolean) { this._socket.setSockOptRaw(SocketOption.DEALER_PROBE, boolBuffer(value)); }
}
export class RouterSocketOptions extends CommonSocketOptions {
  get mandatory(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.ROUTER_MANDATORY), 'mandatory'); }
  set mandatory(value: boolean) { this._socket.setSockOptRaw(SocketOption.ROUTER_MANDATORY, boolBuffer(value)); }
  get handover(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.ROUTER_HANDOVER), 'handover'); }
  set handover(value: boolean) { this._socket.setSockOptRaw(SocketOption.ROUTER_HANDOVER, boolBuffer(value)); }
  get probe(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.PROBE_ROUTER), 'probe'); }
  set probe(value: boolean) { this._socket.setSockOptRaw(SocketOption.PROBE_ROUTER, boolBuffer(value)); }
  set connectRoutingId(value: RoutingId) {
    this._socket.setSockOptRaw(
      SocketOption.CONNECT_ROUTING_ID,
      normalizeRoutingId(value, 'connectRoutingId')
    );
  }
}
export class StreamSocketOptions extends CommonSocketOptions {
  get notify(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.STREAM_NOTIFY), 'notify'); }
  set notify(value: boolean) { this._socket.setSockOptRaw(SocketOption.STREAM_NOTIFY, boolBuffer(value)); }
}
export class PubSocketOptions extends CommonSocketOptions {
  set verbose(value: boolean) { this._socket.setSockOptRaw(SocketOption.XPUB_VERBOSE, boolBuffer(value)); }
  set verboser(value: boolean) { this._socket.setSockOptRaw(SocketOption.XPUB_VERBOSER, boolBuffer(value)); }
  set noDrop(value: boolean) { this._socket.setSockOptRaw(SocketOption.XPUB_NODROP, boolBuffer(value)); }
  set manual(value: boolean) { this._socket.setSockOptRaw(SocketOption.XPUB_MANUAL, boolBuffer(value)); }
}
export class SubSocketOptions extends CommonSocketOptions {
  get topicsCount(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.SUB_TOPICS_COUNT), 'topicsCount'); }
}
export class ContextOptions {
  protected readonly _context: Context;
  constructor(context: Context) { this._context = context; }
  get ioThreads(): number { return this._context.getOptionRawInternal(ContextOption.IO_THREADS); }
  set ioThreads(value: number) { this._context.setOptionRawInternal(ContextOption.IO_THREADS, value | 0); }
  get maxSockets(): number { return this._context.getOptionRawInternal(ContextOption.MAX_SOCKETS); }
  set maxSockets(value: number) { this._context.setOptionRawInternal(ContextOption.MAX_SOCKETS, value | 0); }
  get socketLimit(): number { return this._context.getOptionRawInternal(ContextOption.SOCKET_LIMIT); }
  get maxMsgSize(): number { return this._context.getOptionRawInternal(ContextOption.MAX_MSGSZ); }
  set maxMsgSize(value: number) { this._context.setOptionRawInternal(ContextOption.MAX_MSGSZ, value | 0); }
  get msgTSize(): number { return this._context.getOptionRawInternal(ContextOption.MSG_T_SIZE); }
  get threadPriority(): number { return this._context.getOptionRawInternal(ContextOption.THREAD_PRIORITY); }
  set threadPriority(value: number) { this._context.setOptionRawInternal(ContextOption.THREAD_PRIORITY, value | 0); }
  get threadSchedulingPolicy(): number { return this._context.getOptionRawStrictInternal(ContextOption.THREAD_SCHED_POLICY); }
  set threadSchedulingPolicy(value: number) { this._context.setOptionRawInternal(ContextOption.THREAD_SCHED_POLICY, value | 0); }
  get blocky(): boolean { return this._context.getOptionRawInternal(ContextOption.BLOCKY) !== 0; }
  set blocky(value: boolean) { this._context.setOptionRawInternal(ContextOption.BLOCKY, value ? 1 : 0); }
  addThreadAffinity(cpu: number): void { this._context.setOptionRawInternal(ContextOption.THREAD_AFFINITY_CPU_ADD, cpu | 0); }
  removeThreadAffinity(cpu: number): void { this._context.setOptionRawInternal(ContextOption.THREAD_AFFINITY_CPU_REMOVE, cpu | 0); }
}

export class Context extends NativeHandle {
  readonly options: ContextOptions;
  constructor() {
    super(requireNative().ctxNew());
    if (!this._native) throw lastError('config', 'context creation failed');
    this.options = new ContextOptions(this);
  }
  nativeHandle(): unknown { return this._native; }
  setOptionRawInternal(option: number, value: Buffer | number): void {
    requireNative().ctxSetOpt(this._native, option | 0, typeof value === 'number' ? value | 0 : value);
  }
  getOptionRawInternal(option: number): number {
    try {
      return requireNative().ctxGetOpt(this._native, option | 0) as number;
    } catch (error) {
      if (
        (option | 0) === ContextOption.THREAD_PRIORITY ||
        (option | 0) === ContextOption.THREAD_SCHED_POLICY
      ) {
        return -1;
      }
      throw error;
    }
  }
  getOptionRawStrictInternal(option: number): number {
    try {
      return requireNative().ctxGetOpt(this._native, option | 0) as number;
    } catch (error) {
      const message = error instanceof Error && error.message
        ? error.message
        : 'ctx_getopt failed';
      throw createError('config', readErrno(), message);
    }
  }
  shutdown(): void {
    requireNative().ctxShutdown(this._native);
  }
  close(): void {
    if (!this._native) return;
    requireNative().ctxTerm(this._native);
    this._native = null;
  }
}

export class MonitorSocket extends NativeHandle {
  static readonly ignoreHandler: SocketMonitorHandler = () => {};
  recv(flags: RecvFlags = RecvFlags.None): MonitorEvent {
    if ((flags | 0) & (RecvFlags.DontWait | 0)) {
      const raw = requireNative().monitorRecvNoWait(this._native) as MonitorEventValueRaw | null;
      if (!raw) throw new RecvError(RecvResult.NoData, 11, 'monitor recv failed');
      return new MonitorEvent(raw);
    }
    return new MonitorEvent(requireNative().monitorRecv(this._native) as MonitorEventValueRaw);
  }
  onEvent(handler: SocketMonitorHandler): void {
    requireNative().monitorHandler(this._native, (event: MonitorEventValueRaw) => {
      handler(new MonitorEvent(event));
    });
  }
  snapshot(): MonitorSnapshot {
    return materializeMonitorSnapshot(
      requireNative().monitorSnapshot(this._native) as MonitorSnapshotRaw
    );
  }
  close(): void { if (this._native) { requireNative().monitorClose(this._native); this._native = null; } }
}

export class ServiceMonitor extends NativeHandle {
  recv(flags: RecvFlags = RecvFlags.None): ServiceEvent {
    if ((flags | 0) & (RecvFlags.DontWait | 0)) {
      const raw = requireNative().serviceMonitorRecvNoWait(this._native) as ServiceEventValue | null;
      if (!raw) throw new RecvError(RecvResult.NoData, 11, 'service monitor recv failed');
      return new ServiceEvent(raw);
    }
    return new ServiceEvent(requireNative().serviceMonitorRecv(this._native) as ServiceEventValue);
  }
  onEvent(handler: (event: ServiceEvent) => void): void {
    requireNative().serviceMonitorHandler(this._native, (event: ServiceEventValue) => handler(new ServiceEvent(event)));
  }
  snapshot(): MonitorSnapshot {
    return materializeMonitorSnapshot(
      requireNative().monitorSnapshot(this._native) as MonitorSnapshotRaw
    );
  }
  close(): void { if (this._native) { requireNative().monitorClose(this._native); this._native = null; } }
}

class SendSocket extends ConnectableSocket {
  send(message: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const payload = normalizeMessageLikePayload(message);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      const result = Array.isArray(payload)
        ? requireNative().socketSendNoWaitResultParts(this.nativeHandle(), payload) as number
        : requireNative().socketSendNoWaitResult(this.nativeHandle(), payload) as number;
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    try {
      if (Array.isArray(payload)) {
        requireNative().socketSendParts(this.nativeHandle(), payload, flags | 0);
      } else {
        requireNative().socketSend(this.nativeHandle(), payload, flags | 0);
      }
      return true;
    } catch (error) {
      throw submitNativeError(error, flags, 'send failed');
    }
  }
}

class PublisherSocket extends ConnectableSocket {
  publish(topic: string, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalizedTopic = validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER);
    const normalized = normalizeMessageLikePayload(payload);
    try {
      requireNative().socketPublish(this.nativeHandle(), normalizedTopic, normalized, flags | 0);
      return true;
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'publish failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }
}

class MessageSocket extends SendSocket {
  recv(flags: RecvFlags = RecvFlags.None): Received | null {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null
        : requireNative().socketRecvMessage(this.nativeHandle(), flags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recv failed');
    }
    return raw ? materializeReceived(raw) : null;
  }
  onSendReady(handler: SocketSendReadyHandler): void {
    requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
  }
}

class SubscriberSocket extends ConnectableSocket {
  setSubscription(topicOrPattern: string): void {
    requireNative().socketSetOpt(
      this.nativeHandle(),
      SocketOption.SUBSCRIBE | 0,
      Buffer.from(validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER))
    );
  }
  unsetSubscription(topicOrPattern: string): void {
    requireNative().socketSetOpt(
      this.nativeHandle(),
      SocketOption.UNSUBSCRIBE | 0,
      Buffer.from(validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER))
    );
  }
  subscribe(flags: RecvFlags = RecvFlags.None): TopicMessage | null {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTrySubscribeMessage(this.nativeHandle()) as { topic: string; parts: MessageSnapshot[]; routingId?: Buffer | null } | null
        : requireNative().socketSubscribeMessage(this.nativeHandle(), flags | 0) as { topic: string; parts: MessageSnapshot[]; routingId?: Buffer | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscribe failed');
    }
    return raw ? materializeTopicMessage(raw) : null;
  }
}

class RoutedMessageSocket extends ConnectableSocket {
  send(routingId: RoutingId, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalized = normalizeMessageLikePayload(payload);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      const result = Array.isArray(normalized)
        ? requireNative().socketSendRoutingNoWaitResultParts(this.nativeHandle(), normalizeRoutingId(routingId), normalized) as number
        : requireNative().socketSendRoutingNoWaitResult(this.nativeHandle(), normalizeRoutingId(routingId), normalized) as number;
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    const parts = Array.isArray(normalized)
      ? [normalizeRoutingId(routingId), ...normalized]
      : [normalizeRoutingId(routingId), normalized];
    requireNative().socketSendParts(this.nativeHandle(), parts, flags | 0);
    return true;
  }
  recv(flags: RecvFlags = RecvFlags.None): Received | null {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().routerRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null
        : requireNative().routerRecvMessage(this.nativeHandle(), flags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recv failed');
    }
    return raw ? materializeReceived(raw) : null;
  }
  onSendReady(handler: SocketSendReadyHandler): void {
    requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
  }
}

export class PairSocket extends MessageSocket {
  readonly options: CommonSocketOptions;
  constructor(ctx: Context) { super(ctx, SocketType.PAIR); this.options = new CommonSocketOptions(this); }
}

export class PubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, SocketType.PUB); this.options = new PubSocketOptions(this); }
  onSendReady(handler: SocketSendReadyHandler): void { requireNative().socketSendReadyHandler(this.nativeHandle(), handler); }
  attachDiscovery(discovery: Discovery): void { requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle()); }
}

export class XPubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, SocketType.XPUB); this.options = new PubSocketOptions(this); }
  receiveSubscriptionEvent(flags: RecvFlags = RecvFlags.None): SubscriptionEvent {
    const raw = ((flags | 0) & (RecvFlags.DontWait | 0))
      ? requireNative().socketTrySubscriptionEvent(this.nativeHandle()) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null
      : requireNative().socketSubscriptionEvent(this.nativeHandle(), flags | 0) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null;
    if (!raw) throw new RecvError(RecvResult.NoData, 11, 'receiveSubscriptionEvent failed');
    return new SubscriptionEvent(raw.topic, raw.subscribed, wrapRoutingId(raw.routingId ?? null));
  }
  onSendReady(handler: SocketSendReadyHandler): void { requireNative().socketSendReadyHandler(this.nativeHandle(), handler); }
}

export class SubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, SocketType.SUB); this.options = new SubSocketOptions(this); }
  attachDiscovery(discovery: Discovery): void { requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle()); }
}

export class XSubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, SocketType.XSUB); this.options = new SubSocketOptions(this); }
}

export class DealerSocket extends MessageSocket {
  readonly options: DealerSocketOptions;
  constructor(ctx: Context) { super(ctx, SocketType.DEALER); this.options = new DealerSocketOptions(this); }
  setRoutingId(routingId: RoutingId): void {
    requireNative().socketSetOpt(
      this.nativeHandle(),
      SocketOption.ROUTING_ID | 0,
      normalizeRoutingId(routingId)
    );
  }
  getRoutingId(): RoutingId {
    return RoutingId.fromBytes(
      requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer
    );
  }
  attachDiscovery(discovery: Discovery): void { requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle()); }
  request(message: MessageLike, timeout?: number): Promise<Message[]>;
  request(parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;
  request(message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
  request(parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
  request(
    payloadOrParts: MessageLike | readonly MessageLike[],
    callbackOrTimeout?: RequestResultCallback | number,
    timeout = 0,
    flagsOrTimeout?: SendFlags | number,
    maybeTimeout?: number,
  ): Promise<Message[]> | boolean {
    const parts = Array.isArray(payloadOrParts)
      ? toMessageParts(payloadOrParts)
      : [normalizeMessageLikePayload(payloadOrParts)];
    if (typeof callbackOrTimeout === 'function') {
      const callback = callbackOrTimeout;
      const { flags, timeoutMs } = normalizeCallbackFlagsAndTimeout(flagsOrTimeout, maybeTimeout);
      const releaseProgress = startRequestProgress(
        this.nativeHandle(),
        (handle) => requireNative().socketRequestProgress(handle),
      );
      try {
        requireNative().dealerRequest(
          this.nativeHandle(),
          parts,
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            callback(
              result as RequestResult,
              replyParts ? replyParts.map((part) => Message.from(part)) : []
            );
          },
          flags | 0,
          timeoutMs | 0,
        );
        return true;
      } catch (error) {
        releaseProgress();
        const submitError = submitNativeError(error, flags, 'request failed');
        if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
          return false;
        }
        throw submitError;
      }
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : timeout) ?? 0;
    return new Promise<Message[]>((resolve, reject) => {
      const releaseProgress = startRequestProgress(
        this.nativeHandle(),
        (handle) => requireNative().socketRequestProgress(handle),
      );
      requireNative().dealerRequest(
        this.nativeHandle(),
        parts,
        (result: number, replyParts: Buffer[] | null) => {
          releaseProgress();
          if (result !== RequestResult.Ok) {
            reject(requestErrorFromResult(result as RequestResult, 'request failed'));
            return;
          }
          resolve((replyParts ?? []).map((part) => Message.from(part)));
        },
        SendFlags.None,
        timeoutMs | 0
      );
    });
  }
}

export class RouterSocket extends RoutedMessageSocket {
  readonly options: RouterSocketOptions;
  constructor(ctx: Context) { super(ctx, SocketType.ROUTER); this.options = new RouterSocketOptions(this); }
  setRoutingId(routingId: RoutingId): void {
    requireNative().socketSetOpt(
      this.nativeHandle(),
      SocketOption.ROUTING_ID | 0,
      normalizeRoutingId(routingId)
    );
  }
  getRoutingId(): RoutingId {
    return RoutingId.fromBytes(
      requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer
    );
  }
  attachDiscovery(discovery: Discovery): void { requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle()); }
  request(routingId: RoutingId, message: MessageLike, timeout?: number): Promise<Message[]>;
  request(routingId: RoutingId, parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;
  request(routingId: RoutingId, message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
  request(routingId: RoutingId, parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
  request(
    routingId: RoutingId,
    payloadOrParts: MessageLike | readonly MessageLike[],
    callbackOrTimeout?: RequestResultCallback | number,
    timeout = 0,
    flagsOrTimeout?: SendFlags | number,
    maybeTimeout?: number,
  ): Promise<Message[]> | boolean {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const peer = normalizeRoutingId(routingId);
    if (typeof callbackOrTimeout === 'function') {
      const callback = callbackOrTimeout;
      const { flags, timeoutMs } = normalizeCallbackFlagsAndTimeout(flagsOrTimeout, maybeTimeout);
      const releaseProgress = startRequestProgress(
        this.nativeHandle(),
        (handle) => requireNative().socketRequestProgress(handle),
      );
      try {
        requireNative().routerRequest(
          this.nativeHandle(),
          peer,
          parts,
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            callback(result as RequestResult, replyParts ? replyParts.map((part) => Message.from(part)) : []);
          },
          flags | 0,
          timeoutMs | 0
        );
        return true;
      } catch (error) {
        releaseProgress();
        const submitError = submitNativeError(error, flags, 'request failed');
        if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
          return false;
        }
        throw submitError;
      }
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : timeout) ?? 0;
    return new Promise<Message[]>((resolve, reject) => {
      const releaseProgress = startRequestProgress(
        this.nativeHandle(),
        (handle) => requireNative().socketRequestProgress(handle),
      );
      requireNative().routerRequest(
        this.nativeHandle(),
        peer,
        parts,
        (result: number, replyParts: Buffer[] | null) => {
          releaseProgress();
          if (result !== RequestResult.Ok) {
            reject(requestErrorFromResult(result as RequestResult, 'request failed'));
            return;
          }
          resolve((replyParts ?? []).map((part) => Message.from(part)));
        },
        SendFlags.None,
        timeoutMs | 0
      );
    });
  }
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, message: MessageLike, flags?: SendFlags): boolean;
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, parts: readonly MessageLike[], flags?: SendFlags): boolean;
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    try {
      requireNative().routerSpotSend(
        this.nativeHandle(),
        normalizeRoutingId(destNodeRid),
        normalizeRoutingId(destSpotRid),
        Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
        flags | 0
      );
      return true;
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'sendToSpot failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, message: MessageLike, timeout?: number): Promise<Message[]>;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestResultCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const nodeRid = normalizeRoutingId(destNodeRid);
    const spotRid = normalizeRoutingId(destSpotRid);
    if (typeof callbackOrTimeout === 'function') {
      const { flags, timeoutMs } = normalizeCallbackFlagsAndTimeout(flagsOrTimeout, maybeTimeout);
      const releaseProgress = startRequestProgress(
        this.nativeHandle(),
        (handle) => requireNative().socketRequestProgress(handle),
      );
      try {
        requireNative().routerSpotRequest(
          this.nativeHandle(),
          nodeRid,
          spotRid,
          parts,
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            callbackOrTimeout(result as RequestResult, replyParts ? replyParts.map((part) => Message.from(part)) : []);
          },
          flags | 0,
          timeoutMs | 0
        );
        return true;
      } catch (error) {
        releaseProgress();
        const submitError = submitNativeError(error, flags, 'requestToSpot failed');
        if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
          return false;
        }
        throw submitError;
      }
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : flagsOrTimeout) ?? 0;
    return new Promise<Message[]>((resolve, reject) => {
      const releaseProgress = startRequestProgress(
        this.nativeHandle(),
        (handle) => requireNative().socketRequestProgress(handle),
      );
      requireNative().routerSpotRequest(
        this.nativeHandle(),
        nodeRid,
        spotRid,
        parts,
        (result: number, replyParts: Buffer[] | null) => {
          releaseProgress();
          if (result !== RequestResult.Ok) {
            reject(requestErrorFromResult(result as RequestResult, 'requestToSpot failed'));
            return;
          }
          resolve((replyParts ?? []).map((part) => Message.from(part)));
        },
        0,
        timeoutMs | 0
      );
    });
  }
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    requireNative().routerSpotReply(
      this.nativeHandle(),
      normalizeRoutingId(destNodeRid),
      normalizeRoutingId(destSpotRid),
      requestSeq,
      Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)]
    );
  }
  reply(routingId: RoutingId, requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
  reply(routingId: RoutingId, requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;
  reply(routingId: RoutingId, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    requireNative().routerReply(
      this.nativeHandle(),
      normalizeRoutingId(routingId),
      requestSeq,
      Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)]
    );
  }
}

export class StreamSocket extends BaseSocket {
  readonly options: StreamSocketOptions;
  constructor(ctx: Context) {
    super(ctx, SocketType.STREAM);
    this.options = new StreamSocketOptions(this);
  }
  send(routingId: RoutingId, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalized = normalizeMessageLikePayload(payload);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      const result = Array.isArray(normalized)
        ? requireNative().socketSendRoutingNoWaitResultParts(this.nativeHandle(), normalizeRoutingId(routingId), normalized) as number
        : requireNative().socketSendRoutingNoWaitResult(this.nativeHandle(), normalizeRoutingId(routingId), normalized) as number;
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    const parts = Array.isArray(normalized)
      ? [normalizeRoutingId(routingId), ...normalized]
      : [normalizeRoutingId(routingId), normalized];
    requireNative().socketSendParts(this.nativeHandle(), parts, flags | 0);
    return true;
  }
  recv(flags: RecvFlags = RecvFlags.None): Received | null {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null } | null
        : requireNative().socketRecvMessage(this.nativeHandle(), flags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recv failed');
    }
    return raw ? materializeReceived(raw) : null;
  }
  onPacket(handler: StreamPacketHandler): void {
    requireNative().socketStreamAttach(
      this.nativeHandle(),
      (routingId: Buffer | null, packets: Buffer[]) => {
        const sourceRid = wrapRoutingId(routingId);
        if (!sourceRid) {
          return 0;
        }
        const header = Message.from(packets.length > 1 ? packets[0] : Buffer.alloc(0));
        const body = Message.from(
          packets.length > 1 ? Buffer.concat(packets.slice(1)) : (packets[0] ?? Buffer.alloc(0))
        );
        handler(sourceRid, header, body);
        return 0;
      },
      1
    );
  }
  onSendReady(handler: SocketSendReadyHandler): void {
    requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
  }
  setRoutingId(routingId: RoutingId): void {
    requireNative().socketSetOpt(
      this.nativeHandle(),
      SocketOption.ROUTING_ID | 0,
      normalizeRoutingId(routingId)
    );
  }
  getRoutingId(): RoutingId {
    return RoutingId.fromBytes(
      requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer
    );
  }
}

export class Registry extends NativeHandle {
  private _bound = false;
  constructor(ctx: Context) { super(requireNative().registryNew(ctx.nativeHandle())); }
  nativeHandle(): unknown { return this._native; }
  bind(pub: string, router: string): void {
    requireNative().registrySetEndpoints(this._native, validateCString(pub, 'pubEndpoint'), validateCString(router, 'routerEndpoint'));
    this._bound = true;
  }
  setId(id: number): void { requireNative().registrySetId(this._native, id | 0); }
  addPeer(pub: string): void { requireNative().registryAddPeer(this._native, validateCString(pub, 'pubEndpoint')); }
  setHeartbeat(intervalMs: number, timeoutMs: number): void { requireNative().registrySetHeartbeat(this._native, intervalMs | 0, timeoutMs | 0); }
  setBroadcastInterval(intervalMs: number): void { requireNative().registrySetBroadcastInterval(this._native, intervalMs | 0); }
  setTlsServer(cert: string, key: string, requireClient = 0): void { requireNative().registrySetTlsServer(this._native, validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER), validateCString(key, 'key', Number.MAX_SAFE_INTEGER), requireClient | 0); }
  setTlsClient(ca: string, host: string, trust = 0): void { requireNative().registrySetTlsClient(this._native, validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER), validateCString(host, 'host', Number.MAX_SAFE_INTEGER), trust | 0); }
  statusSnapshot(): RegistryStatus {
    return mapRegistryStatus(requireNative().registryStatusSnapshot(this._native));
  }
  serviceSummarySnapshot(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[] {
    return (requireNative().registryServiceSummarySnapshot(this._native, filter ?? undefined) as Array<Record<string, unknown>>)
      .map((entry) => mapRegistryServiceSummaryEntry(entry as any));
  }
  topologySnapshot(): RegistryTopologyEntry[] {
    return (requireNative().registryTopologySnapshot(this._native) as Array<Record<string, unknown>>)
      .map((entry) => mapRegistryTopologyEntry(entry as any));
  }
  topologyQuery(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    return (requireNative().registryTopologyQuery(this._native, normalizeTopologyFilter(filter)) as Array<Record<string, unknown>>)
      .map((entry) => mapRegistryTopologyEntry(entry as any));
  }
  memberPeers(serviceType: number, serviceName?: string): MemberPeerEntry[] {
    return (requireNative().registryMemberPeers(this._native, serviceType, serviceName ?? '') as Array<Record<string, unknown>>)
      .map((entry) => mapMemberPeerEntry(entry as any));
  }
  memberPeerMetadata(serviceType: number, serviceName: string, serviceRole: number, endpoint: string): Buffer { return requireNative().registryMemberPeerMetadata(this._native, serviceType, validateCString(serviceName, 'serviceName'), serviceRole, validateCString(endpoint, 'endpoint')); }
  close(): void { if (this._native) { requireNative().registryDestroy(this._native); this._native = null; } }
}

export class RegistryQueryClient extends NativeHandle {
  constructor(ctx: Context) { super(requireNative().registryQueryClientNew(ctx.nativeHandle())); }
  connect(endpoint: string): void { requireNative().registryQueryClientConnect(this._native, validateCString(endpoint, 'endpoint')); }
  snapshot(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    return (requireNative().registryQuerySnapshot(this._native, normalizeTopologyFilter(filter)) as Array<Record<string, unknown>>)
      .map((entry) => mapRegistryTopologyEntry(entry as any));
  }
  close(): void { if (this._native) { requireNative().registryQueryDestroy(this._native); this._native = null; } }
}

export class Discovery extends NativeHandle {
  readonly serviceType: number;
  readonly serviceName: string;
  constructor(ctx: Context, serviceType: number, serviceName: string) {
    if (typeof serviceName !== 'string' || serviceName.length === 0) throw new TypeError('Discovery serviceName must be a non-empty string');
    validateCString(serviceName, 'serviceName');
    super(requireNative().discoveryNew(ctx.nativeHandle(), serviceType, serviceName));
    this.serviceType = serviceType;
    this.serviceName = serviceName;
  }
  nativeHandle(): unknown { return this._native; }
  connectRegistry(endpoint: string): void { requireNative().discoveryConnectRegistry(this._native, validateCString(endpoint, 'endpoint')); }
  setValue(value: number): void { requireNative().discoverySetValue(this._native, value | 0); }
  getValue(): number { return requireNative().discoveryGetValue(this._native) as number; }
  setMetadata(metadata: BufferLike | string): void { requireNative().discoverySetMetadata(this._native, normalizeBufferLike(metadata, 'metadata')); }
  getMetadata(): Buffer { return requireNative().discoveryGetMetadata(this._native) as Buffer; }
  resolveSpot(spotRid: RoutingId): RoutingId {
    return RoutingId.fromBytes(
      requireNative().discoveryResolveSpot(this._native, normalizeRoutingId(spotRid)) as Buffer
    );
  }
  setDealerPeerMode(mode: DiscoveryDealerPeerMode): void {
    requireNative().discoverySetDealerPeerMode(this._native, mode | 0);
  }
  memberPeers(): MemberPeerEntry[] {
    return (requireNative().discoveryGetProviders(this._native) as Array<Record<string, unknown>>)
      .map((entry) => mapMemberPeerEntry(entry as any));
  }
  memberPeerMetadata(serviceRole: number, endpoint: string): Buffer { return requireNative().discoveryMemberPeerMetadata(this._native, serviceRole, validateCString(endpoint, 'endpoint')) as Buffer; }
  monitorOpen(events: ServiceMonitorEventMask = ServiceMonitorEventMask.all): ServiceMonitor { return new ServiceMonitor(requireNative().discoveryOpenMonitor(this._native, events | 0)); }
  setTlsClient(ca: string, host: string, trust = 0): void { requireNative().discoverySetTlsClient(this._native, validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER), validateCString(host, 'host', Number.MAX_SAFE_INTEGER), trust | 0); }
  close(): void { if (this._native) { requireNative().discoveryDestroy(this._native); this._native = null; } }
}

export class SpotNode extends NativeHandle {
  private readonly _spots = new Set<Spot>();
  private readonly _channelDealers = new Map<string, DealerSocket>();
  private _nodeRoutingId: RoutingId;
  constructor(ctx: Context) {
    super(requireNative().spotNodeNew(ctx.nativeHandle()));
    this._nodeRoutingId = RoutingId.fromBytes(randomBytes(16));
  }
  nativeHandle(): unknown { return this._native; }
  bind(endpoint: string): void { requireNative().spotNodeBind(this._native, validateCString(endpoint, 'endpoint')); }
  connectPeer(endpoint: string): void { requireNative().spotNodeConnectPeerPub(this._native, validateCString(endpoint, 'endpoint')); }
  disconnectPeer(endpoint: string): void { requireNative().spotNodeDisconnectPeerPub(this._native, validateCString(endpoint, 'endpoint')); }
  attachChannelDealer(discovery: Discovery, dealer: DealerSocket): void {
    requireNative().spotNodeAttachChannelDealer(
      this._native,
      discovery.nativeHandle(),
      dealer.nativeHandle()
    );
  }
  attachChannelDealerManual(channelName: string, dealer: DealerSocket): void {
    const normalized = validateCString(channelName, 'channelName');
    requireNative().spotNodeAttachChannelDealerManual(
      this._native,
      normalized,
      dealer.nativeHandle()
    );
    this._channelDealers.set(normalized, dealer);
  }
  attachPubIngress(pub: PubSocket): void {
    requireNative().spotNodeAttachPubIngress(this._native, pub.nativeHandle());
  }
  attachDiscovery(discovery: Discovery): void { requireNative().spotNodeSetDiscovery(this._native, discovery.nativeHandle()); }
  setRoutingId(routingId: RoutingId): void {
    requireNative().handleSetRoutingId(this._native, normalizeRoutingId(routingId));
    this._nodeRoutingId = routingId;
  }
  get routingId(): RoutingId {
    this._nodeRoutingId = RoutingId.fromBytes(requireNative().handleGetRoutingId(this._native) as Buffer);
    return this._nodeRoutingId;
  }
  setTlsServer(cert: string, key: string, requireClient = 0): void { requireNative().spotNodeSetTlsServer(this._native, validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER), validateCString(key, 'key', Number.MAX_SAFE_INTEGER), requireClient | 0); }
  setTlsClient(ca: string, host: string, trust = 0): void { requireNative().spotNodeSetTlsClient(this._native, validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER), validateCString(host, 'host', Number.MAX_SAFE_INTEGER), trust | 0); }
  createSpot(): Spot {
    const spot = Spot.create(this);
    this._spots.add(spot);
    try {
      const raw = requireNative().spotNodeStatusSnapshot(this._native) as {
        nodeRoutingId?: Buffer | null;
      };
      if (raw?.nodeRoutingId) {
        this._nodeRoutingId = RoutingId.fromBytes(raw.nodeRoutingId);
      }
    } catch {
      // Ignore cache warm-up failure; statusSnapshot() will surface real errors later.
    }
    return spot;
  }
  unregisterSpot(spot: Spot): void {
    this._spots.delete(spot);
  }
  channelDealerHandle(channelName: string): unknown | null {
    const dealer = this._channelDealers.get(channelName);
    return dealer ? dealer.nativeHandle() : null;
  }
  statusSnapshot(): SpotNodeStatus {
    const raw = requireNative().spotNodeStatusSnapshot(this._native) as {
      serviceName: string;
      localEndpoint: string;
      nodeRoutingId?: Buffer | null;
      state: number;
      configuredPeerCount: number;
      activePeerCount: number;
      connectedPeerCount: number;
      subjectCount: number;
      readySubjectCount: number;
      lastError: number;
      lastChangedMs: number | bigint;
    };
    if (raw.nodeRoutingId) {
      this._nodeRoutingId = RoutingId.fromBytes(raw.nodeRoutingId);
    }
    return mapSpotNodeStatus(raw, this._nodeRoutingId);
  }
  peersSnapshot(): SpotNodePeerEntry[] {
    return (requireNative().spotNodePeersSnapshot(this._native) as Array<Record<string, unknown>>)
      .map((entry) => mapSpotNodePeerEntry(entry as any));
  }
  peersQuery(filter?: SpotNodePeerFilter): SpotNodePeerEntry[] {
    return (requireNative().spotNodePeersQuery(this._native, filter ?? undefined) as Array<Record<string, unknown>>)
      .map((entry) => mapSpotNodePeerEntry(entry as any));
  }
  subjectsSnapshot(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[] {
    return (requireNative().spotNodeSubjectsSnapshot(this._native, filter ?? undefined) as Array<Record<string, unknown>>)
      .map((entry) => mapSpotNodeSubjectEntry(entry as any));
  }
  close(): void {
    if (!this._native) {
      return;
    }
    for (const spot of [...this._spots]) {
      spot.close();
    }
    requireNative().spotNodeDestroy(this._native);
    this._native = null;
  }
}

export class Spot extends NativeHandle {
  private static readonly CREATE_TOKEN = Symbol('Spot.create');
  private readonly _node: SpotNode;
  private constructor(node: SpotNode, token: symbol) {
    if (token !== Spot.CREATE_TOKEN) {
      throw new TypeError('Spot instances must be created with SpotNode.createSpot()');
    }
    super(requireNative().spotNew(node.nativeHandle()));
    this._node = node;
  }

  /** @internal */
  static create(node: SpotNode): Spot {
    return new Spot(node, Spot.CREATE_TOKEN);
  }
  setRoutingId(routingId: RoutingId): void {
    requireNative().handleSetRoutingId(this._native, normalizeRoutingId(routingId));
  }
  get routingId(): RoutingId {
    return RoutingId.fromBytes(requireNative().handleGetRoutingId(this._native) as Buffer);
  }
  getAdmissionState(): AdmissionState {
    return requireNative().handleGetAdmissionState(this._native) as AdmissionState;
  }
  setAdmissionState(state: AdmissionState): void {
    requireNative().handleSetAdmissionState(this._native, state | 0);
  }
  publish(serviceName: string, topic: string, payload: MessageLike, flags?: SendFlags): boolean;
  publish(serviceName: string, topic: string, payloadParts: readonly MessageLike[], flags?: SendFlags): boolean;
  publish(
    serviceName: string,
    topic: string,
    payloadOrParts: MessageLike | readonly MessageLike[],
    flags: SendFlags = SendFlags.None
  ): boolean {
    try {
      requireNative().spotPublish(
        this._native,
        validateCString(serviceName, 'serviceName', Number.MAX_SAFE_INTEGER),
        validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER),
        Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
        flags | 0
      );
      return true;
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'spot publish failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }
  setSubscription(topicOrPattern: string): void { requireNative().spotSubscribe(this._native, validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER)); }
  unsetSubscription(topicOrPattern: string): void { requireNative().spotUnsubscribe(this._native, validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER)); }
  subscribe(flags: RecvFlags = RecvFlags.None): TopicMessage | null {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().spotRecvNoWait(this._native) as any
        : requireNative().spotRecv(this._native, flags | 0) as any;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscribe failed');
    }
    return raw ? materializeTopicMessage(raw) : null;
  }
  receiveSubscriptionEvent(flags: RecvFlags = RecvFlags.None): SubscriptionEvent {
    const raw = ((flags | 0) & (RecvFlags.DontWait | 0))
      ? requireNative().spotSubscriptionEventNoWait(this._native) as {
          routingId?: Buffer | null;
          serviceName?: string | null;
          topic: string;
          subscribed: boolean;
        } | null
      : requireNative().spotSubscriptionEvent(this._native, flags | 0) as {
          routingId?: Buffer | null;
          serviceName?: string | null;
          topic: string;
          subscribed: boolean;
        } | null;
    if (!raw) throw new RecvError(RecvResult.NoData, 11, 'receiveSubscriptionEvent failed');
    return new SubscriptionEvent(
      raw.topic,
      raw.subscribed,
      wrapRoutingId(raw.routingId ?? null),
      raw.serviceName ?? null
    );
  }
  onSendReady(handler: SpotSendReadyHandler): void { requireNative().spotSendReadyHandler(this._native, handler); }
  sendChannel(channelName: string, message: MessageLike, flags?: SendFlags): boolean;
  sendChannel(channelName: string, parts: readonly MessageLike[], flags?: SendFlags): boolean;
  sendChannel(channelName: string, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    try {
      requireNative().spotSendChannel(
        this._native,
        validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER),
        Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
        flags | 0
      );
      return true;
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'spot sendChannel failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }
  requestChannel(channelName: string, message: MessageLike, timeout?: number): Promise<Message[]>;
  requestChannel(channelName: string, parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;
  requestChannel(channelName: string, message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
  requestChannel(channelName: string, parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): boolean;
  requestChannel(channelName: string, payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestResultCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const normalizedChannelName = validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER);
    const progressHandle = this._native;
    const progressPump = (handle: unknown) => requireNative().spotRequestProgress(handle);
    if (typeof callbackOrTimeout === 'function') {
      const { flags, timeoutMs } = normalizeCallbackFlagsAndTimeout(flagsOrTimeout, maybeTimeout);
      const releaseProgress = startRequestProgress(progressHandle, progressPump);
      try {
        requireNative().spotRequestChannel(
          this._native,
          normalizedChannelName,
          parts,
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            callbackOrTimeout(result as RequestResult, replyParts ? replyParts.map((part) => Message.from(part)) : []);
          },
          flags | 0,
          timeoutMs | 0
        );
        return true;
      } catch (error) {
        releaseProgress();
        const submitError = submitNativeError(error, flags, 'requestChannel failed');
        if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
          return false;
        }
        throw submitError;
      }
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : flagsOrTimeout) ?? 0;
    return new Promise<Message[]>((resolve, reject) => {
      const releaseProgress = startRequestProgress(progressHandle, progressPump);
      requireNative().spotRequestChannel(
        this._native,
        normalizedChannelName,
        parts,
        (result: number, replyParts: Buffer[] | null) => {
          releaseProgress();
          if (result !== RequestResult.Ok) {
            reject(requestErrorFromResult(result as RequestResult, 'requestChannel failed'));
            return;
          }
          resolve((replyParts ?? []).map((part) => Message.from(part)));
        },
        0,
        timeoutMs | 0
      );
    });
  }
  private replyToSpotInternal(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, parts: readonly Message[], flags: SendFlags): void {
    normalizeReplyFlags(flags);
    requireNative().spotReplySpot(
      this._native,
      normalizeRoutingId(destNodeRid),
      normalizeRoutingId(destSpotRid),
      requestSeq,
      parts.map((part) => part.data())
    );
  }
  private replyToRouterInternal(peerRid: RoutingId, requestSeq: bigint, parts: readonly Message[], flags: SendFlags): void {
    normalizeReplyFlags(flags);
    requireNative().spotReplyRouter(
      this._native,
      normalizeRoutingId(peerRid),
      requestSeq,
      parts.map((part) => part.data())
    );
  }
  recvRouted(flags: RecvFlags = RecvFlags.None): Received | null {
    let raw;
    try {
      raw = requireNative().spotRecvRouted(this._native, flags | 0) as { sourceRid?: Buffer | null; spotRid?: Buffer | null; requestSeq?: bigint | null; parts: MessageSnapshot[] } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recvRouted failed');
    }
    if (!raw) return null;
    return materializeReceived(
      {
        parts: raw.parts,
        routingId: raw.sourceRid ?? null,
        requestSeq: raw.requestSeq ?? null,
        spotRid: raw.spotRid ?? null
      },
      (requestSeq, parts, flags) => {
        if (!raw.sourceRid) {
          throw submitErrorFromResult(SubmitResult.InvalidState, 'missing routed reply target');
        }
        const sourceRid = RoutingId.fromBytes(raw.sourceRid);
        if (raw.spotRid) {
          this.replyToSpotInternal(sourceRid, RoutingId.fromBytes(raw.spotRid), requestSeq, parts, flags);
          return;
        }
        this.replyToRouterInternal(sourceRid, requestSeq, parts, flags);
      }
    );
  }
  onRoutedReceive(handler: SpotRoutedHandler): void {
    requireNative().spotRoutedHandler(this._native, (sourceRid: Buffer | null, spotRid: Buffer | null, requestSeq: bigint, parts: Buffer[]) => {
      handler(
        wrapRoutingId(sourceRid),
        wrapRoutingId(spotRid),
        requestSeq,
        parts.map((part) => Message.from(part))
      );
    });
  }
  onDispatchEvent(handler: SpotDispatchEventHandler): void {
    requireNative().spotDispatchEventHandler(this._native, handler);
  }
  drainChannelReplyFrom(subjectHandle: bigint): void {
    requireNative().spotChannelReplyProgress(this._native, subjectHandle);
  }
  setLinger(milliseconds: number): void { requireNative().socketSetOpt(this._native, SocketOption.LINGER, int32Buffer(milliseconds, 'milliseconds')); }
  setSendHighWaterMark(value: number): void { requireNative().socketSetOpt(this._native, SocketOption.SNDHWM, int32Buffer(value, 'value')); }
  setReceiveHighWaterMark(value: number): void { requireNative().socketSetOpt(this._native, SocketOption.RCVHWM, int32Buffer(value, 'value')); }
  setSendTimeout(milliseconds: number): void { requireNative().socketSetOpt(this._native, SocketOption.SNDTIMEO, int32Buffer(milliseconds, 'milliseconds')); }
  setReceiveTimeout(milliseconds: number): void { requireNative().socketSetOpt(this._native, SocketOption.RCVTIMEO, int32Buffer(milliseconds, 'milliseconds')); }
  setNoDrop(enabled: boolean): void { requireNative().socketSetOpt(this._native, SocketOption.XPUB_NODROP, boolBuffer(enabled)); }
  close(): void {
    if (this._native) {
      requireNative().spotDestroy(this._native);
      this._native = null;
      this._node.unregisterSpot(this);
    }
  }
}

export interface PollerEvent {
  readonly sourceKind: number;
  readonly socket: BaseSocket | null;
  readonly fd: number;
  readonly timer: Timer | null;
  readonly userData: any;
  readonly events: number;
}

export class Poller {
  private _native: unknown | null;
  constructor() { this._native = requireNative().pollerNew(); }
  addSocket(socket: BaseSocket, events: number, userData?: any): void { requireNative().pollerAdd(this._native, socket.nativeHandle(), userData ?? null, events | 0); }
  modifySocket(socket: BaseSocket, events: number): void { requireNative().pollerModify(this._native, socket.nativeHandle(), events | 0); }
  removeSocket(socket: BaseSocket): void { requireNative().pollerRemove(this._native, socket.nativeHandle()); }
  addFd(fd: number, events: number, userData?: any): void { requireNative().pollerAddFd(this._native, fd | 0, userData ?? null, events | 0); }
  modifyFd(fd: number, events: number): void { requireNative().pollerModifyFd(this._native, fd | 0, events | 0); }
  removeFd(fd: number): void { requireNative().pollerRemoveFd(this._native, fd | 0); }
  addTimer(timer: Timer, userData?: any): void { requireNative().pollerAddTimer(this._native, timer.nativeHandle(), userData ?? null); }
  removeTimer(timer: Timer): void { requireNative().pollerRemoveTimer(this._native, timer.nativeHandle()); }
  get size(): number { return requireNative().pollerSize(this._native) as number; }
  wait(timeoutMs: number): PollerEvent | null {
    try {
      return requireNative().pollerWait(this._native, timeoutMs | 0) as PollerEvent | null;
    } catch (error) {
      const message = error instanceof Error && error.message ? error.message : String(error);
      if (/Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
        return null;
      }
      throw error;
    }
  }
  waitAll(events: number, timeoutMs: number): PollerEvent[] {
    try {
      return requireNative().pollerWaitAll(this._native, events | 0, timeoutMs | 0) as PollerEvent[];
    } catch (error) {
      const message = error instanceof Error && error.message ? error.message : String(error);
      if (/Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
        return [];
      }
      throw error;
    }
  }
  poll(timeoutMs: number): number[] {
    let ready;
    try {
      ready = requireNative().pollerWaitAll(this._native, this.size, timeoutMs | 0) as Array<{ events: number }>;
    } catch (error) {
      const message = error instanceof Error && error.message ? error.message : String(error);
      if (/Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
        return [];
      }
      throw error;
    }
    return ready.map((event) => event.events | 0);
  }
  destroy(): void { if (this._native) { requireNative().pollerDestroy(this._native); this._native = null; } }
  close(): void { this.destroy(); }
}

export class Timer extends NativeHandle {
  constructor(native?: unknown) { super(native ?? requireNative().timerNew()); }
  static fromSpot(spot: Spot): Timer { return new Timer(requireNative().spotTimerNew(spot.nativeHandle())); }
  start(intervalNs: bigint, repeatCount: bigint): void { requireNative().timerStart(this._native, intervalNs, repeatCount); }
  stop(): void { requireNative().timerStop(this._native); }
  recv(flags: number = 0): bigint { return requireNative().timerRecv(this._native, flags | 0) as bigint; }
  onFire(handler: TimerHandler): void { requireNative().timerHandler(this._native, (fireCount: bigint) => handler(this, fireCount)); }
  close(): void { if (this._native) { requireNative().timerDestroy(this._native); this._native = null; } }
}

export class Stopwatch extends NativeHandle {
  constructor() { super(requireNative().stopwatchStart()); }
  intermediate(): number { return requireNative().stopwatchIntermediate(this._native) as number; }
  stop(): number { return requireNative().stopwatchStop(this._native) as number; }
  close(): void { this._native = null; }
}

export function version(): [number, number, number] {
  return requireNative().version() as [number, number, number];
}

export function strerror(code: number): string { return requireNative().strerror(code) as string; }
export function has(capability: string): boolean { return requireNative().has(validateCString(capability, 'capability', Number.MAX_SAFE_INTEGER)) as boolean; }
export function proxy(frontend: BaseSocket, backend: BaseSocket, capture?: BaseSocket | null): void { requireNative().proxy(frontend.nativeHandle(), backend.nativeHandle(), capture ? capture.nativeHandle() : null); }
export function proxySteerable(frontend: BaseSocket, backend: BaseSocket, capture: BaseSocket | null, control: BaseSocket): void { requireNative().proxySteerable(frontend.nativeHandle(), backend.nativeHandle(), capture ? capture.nativeHandle() : null, control.nativeHandle()); }
export function sleep(seconds: number): void { requireNative().sleep(seconds | 0); }
export function multipartClose(parts: Message[]): void { for (const part of parts) part.close(); }
