// SPDX-License-Identifier: MPL-2.0

import { requireNative } from './native';
import { normalizeBufferLike, type BufferLike } from './buffer_like';
import {
  Message,
  Received,
  Subscribed,
  SubscriptionEvent,
  METADATA_KEY_USER_MIN,
  METADATA_VALUE_MAX,
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
  Subscribed,
  SubscriptionEvent,
  SocketType,
  SocketOption,
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
  ConfigError,
  METADATA_KEY_USER_MIN,
  METADATA_VALUE_MAX
};

export const ContextOption = Object.freeze({
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

export const MonitorEvent = Object.freeze({
  CONNECTED: 0x0001, CONNECT_DELAYED: 0x0002, CONNECT_RETRIED: 0x0004,
  LISTENING: 0x0008, BIND_FAILED: 0x0010, ACCEPTED: 0x0020,
  ACCEPT_FAILED: 0x0040, CLOSED: 0x0080, CLOSE_FAILED: 0x0100,
  DISCONNECTED: 0x0200, MONITOR_STOPPED: 0x0400,
  HANDSHAKE_FAILED_NO_DETAIL: 0x0800,
  CONNECTION_READY: 0x1000, HANDSHAKE_FAILED_PROTOCOL: 0x2000,
  HANDSHAKE_FAILED_AUTH: 0x4000,
  ALL: 0x7FFF
} as const);

export const ServiceMonitorEvent = Object.freeze({
  ERROR: 1 << 4,
  DISCOVERY_SERVICE_UP: 1 << 5,
  DISCOVERY_SERVICE_DOWN: 1 << 6,
  DISCOVERY_PROVIDERS_CHANGED: 1 << 7,
  CLOSED: 1 << 17,
  ALL: (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 17)
} as const);
export type ServiceMonitorEventMask = number;

export const ServiceType = Object.freeze({ SPOT: 0x3002, SOCKET: 0x3003 } as const);
export const SERVICE_TYPE_SPOT = ServiceType.SPOT;
export const SERVICE_TYPE_SOCKET = ServiceType.SOCKET;
export const ServiceRole = Object.freeze({
  INVALID: 0, SPOT: 2, ROUTER: 3, DEALER: 4, PUB: 5, SUB: 6
} as const);
export const ServiceKind = Object.freeze({
  DISCOVERY: 1, SPOT_SUB: 3, SPOT_PUB: 4, SOCKET: 5
} as const);
export const SpotPeerSource = Object.freeze({ MANUAL: 1, DISCOVERY: 2, MIXED: 3 } as const);
export const SpotPeerState = Object.freeze({ CONFIGURED: 1, CONNECTING: 2, CONNECTED: 3 } as const);
export const SpotNodeState = Object.freeze({ IDLE: 1, CONNECTING: 2, PARTIAL_READY: 3, READY: 4, ERROR: 5 } as const);
export const RegistryState = Object.freeze({ IDLE: 1, ACTIVE: 2, DEGRADED: 3, ERROR: 4 } as const);
export const TopologySource = Object.freeze({ MANUAL: 1, DISCOVERY: 2, REGISTRY: 3 } as const);
export const TopologyState = Object.freeze({ DISCOVERED: 1, CONNECTING: 2, READY: 3, LOST: 4, ERROR: 5, STOPPED: 6 } as const);

export interface MonitorSnapshot {
  sourceKind: number;
  stateFlags: number;
  detailFlags: number;
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
    Object.assign(this, raw);
  }
}

export type SocketRecvHandler = (routingId: Buffer | null, parts: Message[]) => void;
export type SocketSubscribeHandler = (
  routingId: Buffer | null,
  topic: string,
  parts: Message[]
) => void;
export type SocketSendReadyHandler = () => void;
export type SocketMonitorHandler = (event: SocketMonitorEventValue) => void;
export type SpotSubHandler = SocketSubscribeHandler;
export type SpotSendReadyHandler = () => void;
export type SpotRoutedHandler = (
  sourceRid: Buffer | null,
  spotRid: Buffer | null,
  requestSeq: bigint,
  parts: Message[]
) => void;
export type SpotDispatchEventHandler = (event: number) => void;
export type RouterSpotHandler = SpotRoutedHandler;
export type RequestResultCallback = (result: RequestResult, reply?: Received) => void;
export type TimerHandler = (timer: Timer, fireCount: bigint) => void;

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
  if ((flags & RecvFlags.DontWait) !== 0 && /Resource temporarily unavailable|temporarily unavailable|would block|interrupted system call/i.test(message)) {
    return new RecvError(RecvResult.NoData, readErrno(), message);
  }
  return createError('recv', readErrno(), message) as RecvError;
}

function requestErrorFromResult(result: RequestResult, message: string): RequestError {
  return new RequestError(result, 0, message);
}

function submitErrorFromResult(result: SubmitResult, message: string): SubmitError {
  return new SubmitError(result, 0, message);
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

function normalizeRoutingId(routingId: BufferLike): Buffer {
  const normalized = normalizeBufferLike(routingId, 'routingId');
  if (normalized.length === 0 || normalized.length > 255) {
    throw new RangeError('routingId must be at most 255 bytes');
  }
  return normalized;
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

  connect(endpoint: string): void {
    requireNative().socketConnect(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
  }

  disconnect(endpoint: string): void {
    requireNative().socketDisconnect(this.nativeHandle(), validateCString(endpoint, 'endpoint'));
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

  close(): void {
    if (!this._native) return;
    requireNative().socketClose(this._native);
    this._native = null;
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
  set connectRoutingId(value: BufferLike) { this._socket.setSockOptRaw(SocketOption.CONNECT_ROUTING_ID, normalizeBufferLike(value, 'connectRoutingId')); }
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
  private _threadSchedulingPolicy = 0;
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
  get threadSchedulingPolicy(): number {
    try {
      return this._context.getOptionRawInternal(ContextOption.THREAD_SCHED_POLICY);
    } catch {
      return this._threadSchedulingPolicy;
    }
  }
  set threadSchedulingPolicy(value: number) {
    this._threadSchedulingPolicy = value | 0;
    this._context.setOptionRawInternal(ContextOption.THREAD_SCHED_POLICY, this._threadSchedulingPolicy);
  }
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
  recv(): SocketMonitorEventValue {
    return requireNative().monitorRecv(this._native) as SocketMonitorEventValue;
  }
  onEvent(handler: SocketMonitorHandler): void {
    requireNative().monitorHandler(this._native, handler);
  }
  snapshot(): MonitorSnapshot {
    return requireNative().monitorSnapshot(this._native) as MonitorSnapshot;
  }
  close(): void { if (this._native) { requireNative().monitorClose(this._native); this._native = null; } }
}

export class ServiceMonitor extends NativeHandle {
  recv(): ServiceEvent { return new ServiceEvent(requireNative().serviceMonitorRecv(this._native) as ServiceEventValue); }
  onEvent(handler: (event: ServiceEvent) => void): void {
    requireNative().serviceMonitorHandler(this._native, (event: ServiceEventValue) => handler(new ServiceEvent(event)));
  }
  snapshot(): MonitorSnapshot { return requireNative().monitorSnapshot(this._native) as MonitorSnapshot; }
  close(): void { if (this._native) { requireNative().monitorClose(this._native); this._native = null; } }
}

class SendSocket extends BaseSocket {
  send(message: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    const payload = normalizeMessageLikePayload(message);
    if (Array.isArray(payload)) {
      requireNative().socketSendParts(this.nativeHandle(), payload, flags | 0);
    } else {
      requireNative().socketSend(this.nativeHandle(), payload, flags | 0);
    }
  }
}

class PublisherSocket extends BaseSocket {
  publish(topic: string, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    const normalizedTopic = validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER);
    const normalized = normalizeMessageLikePayload(payload);
    requireNative().socketPublish(this.nativeHandle(), normalizedTopic, normalized, flags | 0);
  }
}

class MessageSocket extends SendSocket {
  recv(flags: RecvFlags = RecvFlags.None): Received {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTryRecvMessage(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null
        : requireNative().socketRecvMessage(this.nativeHandle(), flags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recv failed');
    }
    if (!raw) throw new RecvError(RecvResult.NoData, 11, 'recv failed');
    return new Received(Object.freeze(raw.parts.map((part) => Message.fromSnapshot(part))), raw.routingId ?? null, raw.requestSeq ?? null);
  }
  onReceive(handler: SocketRecvHandler): void {
    requireNative().socketRecvHandler(this.nativeHandle(), (routingId: Buffer | null, parts: Buffer[]) => handler(routingId, parts.map((part) => Message.from(part))));
  }
  onSendReady(handler: SocketSendReadyHandler): void {
    requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
  }
}

class SubscriberSocket extends BaseSocket {
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
  subscribe(flags: RecvFlags = RecvFlags.None): Subscribed {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTrySubscribeMessage(this.nativeHandle()) as { topic: string; parts: MessageSnapshot[]; routingId?: Buffer | null } | null
        : requireNative().socketSubscribeMessage(this.nativeHandle(), flags | 0) as { topic: string; parts: MessageSnapshot[]; routingId?: Buffer | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscribe failed');
    }
    if (!raw) throw new RecvError(RecvResult.NoData, 11, 'subscribe failed');
    return new Subscribed(raw.topic, raw.parts.map((part) => Message.fromSnapshot(part)), raw.routingId ?? null);
  }
  onSubscribe(handler: SocketSubscribeHandler): void {
    requireNative().socketSubscribeHandler(this.nativeHandle(), (routingId: Buffer | null, topic: string, parts: Buffer[]) => handler(routingId, topic, parts.map((part) => Message.from(part))));
  }
}

class RoutedMessageSocket extends BaseSocket {
  send(routingId: BufferLike, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    const normalized = normalizeMessageLikePayload(payload);
    const parts = Array.isArray(normalized)
      ? [normalizeRoutingId(routingId), ...normalized]
      : [normalizeRoutingId(routingId), normalized];
    requireNative().socketSendParts(this.nativeHandle(), parts, flags | 0);
  }
  recv(flags: RecvFlags = RecvFlags.None): Received {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().routerTryRecvMessage(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null
        : requireNative().routerRecvMessage(this.nativeHandle(), flags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recv failed');
    }
    if (!raw) throw new RecvError(RecvResult.NoData, 11, 'recv failed');
    return new Received(raw.parts.map((part) => Message.fromSnapshot(part)), raw.routingId ?? null, raw.requestSeq ?? null);
  }
  onReceive(handler: SocketRecvHandler): void {
    requireNative().routerHandlerMessage(this.nativeHandle(), (raw: { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null }) => handler(raw.routingId ?? null, raw.parts.map((part) => Message.fromSnapshot(part))));
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
    return new SubscriptionEvent(raw.topic, raw.subscribed, raw.routingId ?? null);
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
  setRoutingId(routingId: BufferLike): void { requireNative().socketSetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0, normalizeRoutingId(routingId)); }
  getRoutingId(): Buffer { return requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer; }
  attachDiscovery(discovery: Discovery): void { requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle()); }
}

export class RouterSocket extends RoutedMessageSocket {
  readonly options: RouterSocketOptions;
  constructor(ctx: Context) { super(ctx, SocketType.ROUTER); this.options = new RouterSocketOptions(this); }
  setRoutingId(routingId: BufferLike): void { requireNative().socketSetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0, normalizeRoutingId(routingId)); }
  getRoutingId(): Buffer { return requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer; }
  attachDiscovery(discovery: Discovery): void { requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle()); }
  sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, message: MessageLike, flags?: SendFlags): void;
  sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;
  sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    requireNative().routerSpotSend(
      this.nativeHandle(),
      normalizeRoutingId(destNodeRid),
      normalizeRoutingId(destSpotRid),
      Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
      flags | 0
    );
  }
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, message: MessageLike, timeout?: number): Promise<Received>;
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, parts: readonly MessageLike[], timeout?: number): Promise<Received>;
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestResultCallback | number, flags: SendFlags = SendFlags.None, timeout = 0): Promise<Received> | void {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const nodeRid = normalizeRoutingId(destNodeRid);
    const spotRid = normalizeRoutingId(destSpotRid);
    if (typeof callbackOrTimeout === 'function') {
      return void requireNative().routerSpotRequest(
        this.nativeHandle(),
        nodeRid,
        spotRid,
        parts,
        (result: number, replyParts: Buffer[] | null) => callbackOrTimeout(result as RequestResult, replyParts ? new Received(replyParts.map((part) => Message.from(part))) : null),
        flags | 0,
        timeout | 0
      );
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : timeout) ?? 0;
    return new Promise<Received>((resolve, reject) => {
      requireNative().routerSpotRequest(
        this.nativeHandle(),
        nodeRid,
        spotRid,
        parts,
        (result: number, replyParts: Buffer[] | null) => {
          if (result !== RequestResult.Ok) {
            reject(requestErrorFromResult(result as RequestResult, 'requestToSpot failed'));
            return;
          }
          resolve(new Received((replyParts ?? []).map((part) => Message.from(part))));
        },
        0,
        timeoutMs | 0
      );
    });
  }
  replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
  replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;
  replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    requireNative().routerSpotReply(
      this.nativeHandle(),
      normalizeRoutingId(destNodeRid),
      normalizeRoutingId(destSpotRid),
      requestSeq,
      Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
      flags | 0
    );
  }
  recvSpot(flags: RecvFlags = RecvFlags.None): Received {
    const raw = requireNative().routerSpotRecv(this.nativeHandle(), flags | 0) as { sourceNodeRid?: Buffer | null; sourceSpotRid?: Buffer | null; requestSeq?: bigint | null; parts: MessageSnapshot[] } | null;
    if (!raw) throw lastError('recv', 'recvSpot failed');
    return new Received(raw.parts.map((part) => Message.fromSnapshot(part)), raw.sourceNodeRid ?? raw.sourceSpotRid ?? null, raw.requestSeq ?? null);
  }
  onSpotReceive(handler: RouterSpotHandler): void {
    requireNative().routerSpotHandler(this.nativeHandle(), (sourceNodeRid: Buffer | null, sourceSpotRid: Buffer | null, requestSeq: bigint, parts: Buffer[]) => handler(sourceNodeRid, sourceSpotRid, requestSeq, parts.map((part) => Message.from(part))));
  }
  reply(routingId: BufferLike, requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
  reply(routingId: BufferLike, requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;
  reply(routingId: BufferLike, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    requireNative().routerReply(this.nativeHandle(), normalizeRoutingId(routingId), requestSeq, Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)], flags | 0);
  }
}

export class StreamSocket extends RoutedMessageSocket {
  readonly options: StreamSocketOptions;
  constructor(ctx: Context) {
    super(ctx, SocketType.STREAM);
    this.options = new StreamSocketOptions(this);
    Object.defineProperty(this, 'connect', { value: undefined, configurable: true, enumerable: false, writable: false });
    Object.defineProperty(this, 'disconnect', { value: undefined, configurable: true, enumerable: false, writable: false });
  }
  setRoutingId(routingId: BufferLike): void { requireNative().socketSetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0, normalizeRoutingId(routingId)); }
  getRoutingId(): Buffer { return requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer; }
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
  statusSnapshot(): any { return requireNative().registryStatusSnapshot(this._native); }
  serviceSummarySnapshot(filter?: any): any[] { return requireNative().registryServiceSummarySnapshot(this._native, filter ?? undefined); }
  topologySnapshot(): any[] { return requireNative().registryTopologySnapshot(this._native); }
  topologyQuery(filter?: any): any[] { return requireNative().registryTopologyQuery(this._native, filter ?? undefined); }
  memberPeers(serviceType: number, serviceName?: string): any[] { return requireNative().registryMemberPeers(this._native, serviceType, serviceName ?? ''); }
  memberPeerMetadata(serviceType: number, serviceName: string, serviceRole: number, endpoint: string): Buffer { return requireNative().registryMemberPeerMetadata(this._native, serviceType, validateCString(serviceName, 'serviceName'), serviceRole, validateCString(endpoint, 'endpoint')); }
  close(): void { if (this._native) { requireNative().registryDestroy(this._native); this._native = null; } }
}

export class RegistryQueryClient extends NativeHandle {
  constructor(ctx: Context) { super(requireNative().registryQueryClientNew(ctx.nativeHandle())); }
  connect(endpoint: string): void { requireNative().registryQueryClientConnect(this._native, validateCString(endpoint, 'endpoint')); }
  snapshot(filter?: any): any[] { return requireNative().registryQuerySnapshot(this._native, filter ?? undefined); }
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
  memberPeers(): any[] { return requireNative().discoveryGetProviders(this._native) as any[]; }
  memberPeerMetadata(serviceRole: number, endpoint: string): Buffer { return requireNative().discoveryMemberPeerMetadata(this._native, serviceRole, validateCString(endpoint, 'endpoint')) as Buffer; }
  monitorOpen(events: ServiceMonitorEventMask = ServiceMonitorEvent.ALL): ServiceMonitor { return new ServiceMonitor(requireNative().discoveryOpenMonitor(this._native, events | 0)); }
  setTlsClient(ca: string, host: string, trust = 0): void { requireNative().discoverySetTlsClient(this._native, validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER), validateCString(host, 'host', Number.MAX_SAFE_INTEGER), trust | 0); }
  close(): void { if (this._native) { requireNative().discoveryDestroy(this._native); this._native = null; } }
}

export class SpotNode extends NativeHandle {
  constructor(ctx: Context) { super(requireNative().spotNodeNew(ctx.nativeHandle())); }
  nativeHandle(): unknown { return this._native; }
  bind(endpoint: string): void { requireNative().spotNodeBind(this._native, validateCString(endpoint, 'endpoint')); }
  connectPeer(endpoint: string): void { requireNative().spotNodeConnectPeerPub(this._native, validateCString(endpoint, 'endpoint')); }
  disconnectPeer(endpoint: string): void { requireNative().spotNodeDisconnectPeerPub(this._native, validateCString(endpoint, 'endpoint')); }
  attachDiscovery(discovery: Discovery): void { requireNative().spotNodeSetDiscovery(this._native, discovery.nativeHandle()); }
  setTlsServer(cert: string, key: string, requireClient = 0): void { requireNative().spotNodeSetTlsServer(this._native, validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER), validateCString(key, 'key', Number.MAX_SAFE_INTEGER), requireClient | 0); }
  setTlsClient(ca: string, host: string, trust = 0): void { requireNative().spotNodeSetTlsClient(this._native, validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER), validateCString(host, 'host', Number.MAX_SAFE_INTEGER), trust | 0); }
  statusSnapshot(): any { return requireNative().spotNodeStatusSnapshot(this._native); }
  peersSnapshot(): any[] { return requireNative().spotNodePeersSnapshot(this._native); }
  peersQuery(filter?: any): any[] { return requireNative().spotNodePeersQuery(this._native, filter ?? undefined); }
  subjectsSnapshot(filter?: any): any[] { return requireNative().spotNodeSubjectsSnapshot(this._native, filter ?? undefined); }
  close(): void { if (this._native) { requireNative().spotNodeDestroy(this._native); this._native = null; } }
}

export class Spot extends NativeHandle {
  private _subscribeCallbackInstalled = false;
  constructor(node: SpotNode) { super(requireNative().spotNew(node.nativeHandle())); }
  publish(topic: string, payload: MessageLike, flags: SendFlags = SendFlags.None): void { requireNative().spotPublish(this._native, validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER), payload instanceof Message ? payload.toSnapshot() : normalizeBufferLike(payload, 'payload'), flags | 0); }
  publishParts(topic: string, payloadParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): void { requireNative().spotPublish(this._native, validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER), toMessageParts(payloadParts), flags | 0); }
  setSubscription(topicOrPattern: string): void { requireNative().spotSubscribe(this._native, validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER)); }
  unsetSubscription(topicOrPattern: string): void { requireNative().spotUnsubscribe(this._native, validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER)); }
  subscribe(flags: RecvFlags = RecvFlags.None): Subscribed {
    let raw;
    try {
      raw = requireNative().spotRecv(this._native, flags | 0) as any;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscribe failed');
    }
    if (!raw) throw lastError('recv', 'subscribe failed');
    return new Subscribed(raw.topic, raw.parts.map((part: MessageSnapshot) => Message.fromSnapshot(part)), raw.routingId ?? null);
  }
  onSubscribe(handler: SpotSubHandler): void { requireNative().spotSubscribeHandler(this._native, (routingId: Buffer | null, topic: string, parts: Buffer[]) => handler(routingId, topic, parts.map((part) => Message.from(part)))); }
  onSendReady(handler: SpotSendReadyHandler): void { requireNative().spotSendReadyHandler(this._native, handler); }
  sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, message: MessageLike, flags?: SendFlags): void;
  sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;
  sendToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    requireNative().spotSendSpot(
      this._native,
      normalizeRoutingId(destNodeRid),
      normalizeRoutingId(destSpotRid),
      Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
      flags | 0
    );
  }
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, message: MessageLike, timeout?: number): Promise<Received>;
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, parts: readonly MessageLike[], timeout?: number): Promise<Received>;
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  requestToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestResultCallback | number, flags: SendFlags = SendFlags.None, timeout = 0): Promise<Received> | void {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const nodeRid = normalizeRoutingId(destNodeRid);
    const spotRid = normalizeRoutingId(destSpotRid);
    if (typeof callbackOrTimeout === 'function') {
      return void requireNative().spotRequestSpot(
        this._native,
        nodeRid,
        spotRid,
        parts,
        (result: number, replyParts: Buffer[] | null) => callbackOrTimeout(result as RequestResult, replyParts ? new Received(replyParts.map((part) => Message.from(part))) : undefined),
        flags | 0,
        timeout | 0
      );
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : timeout) ?? 0;
    return new Promise<Received>((resolve, reject) => {
      requireNative().spotRequestSpot(
        this._native,
        nodeRid,
        spotRid,
        parts,
        (result: number, replyParts: Buffer[] | null) => {
          if (result !== RequestResult.Ok) {
            reject(requestErrorFromResult(result as RequestResult, 'requestToSpot failed'));
            return;
          }
          resolve(new Received((replyParts ?? []).map((part) => Message.from(part))));
        },
        0,
        timeoutMs | 0
      );
    });
  }
  replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
  replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;
  replyToSpot(destNodeRid: BufferLike, destSpotRid: BufferLike, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    requireNative().spotReplySpot(
      this._native,
      normalizeRoutingId(destNodeRid),
      normalizeRoutingId(destSpotRid),
      requestSeq,
      Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
      flags | 0
    );
  }
  sendToRouter(peerRid: BufferLike, message: MessageLike, flags?: SendFlags): void;
  sendToRouter(peerRid: BufferLike, parts: readonly MessageLike[], flags?: SendFlags): void;
  sendToRouter(peerRid: BufferLike, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    requireNative().spotSendRouter(
      this._native,
      normalizeRoutingId(peerRid),
      Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
      flags | 0
    );
  }
  requestToRouter(peerRid: BufferLike, message: MessageLike, timeout?: number): Promise<Received>;
  requestToRouter(peerRid: BufferLike, parts: readonly MessageLike[], timeout?: number): Promise<Received>;
  requestToRouter(peerRid: BufferLike, message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  requestToRouter(peerRid: BufferLike, parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  requestToRouter(peerRid: BufferLike, payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestResultCallback | number, flags: SendFlags = SendFlags.None, timeout = 0): Promise<Received> | void {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const peer = normalizeRoutingId(peerRid);
    if (typeof callbackOrTimeout === 'function') {
      return void requireNative().spotRequestRouter(
        this._native,
        peer,
        parts,
        (result: number, replyParts: Buffer[] | null) => callbackOrTimeout(result as RequestResult, replyParts ? new Received(replyParts.map((part) => Message.from(part))) : undefined),
        flags | 0,
        timeout | 0
      );
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : timeout) ?? 0;
    return new Promise<Received>((resolve, reject) => {
      requireNative().spotRequestRouter(
        this._native,
        peer,
        parts,
        (result: number, replyParts: Buffer[] | null) => {
          if (result !== RequestResult.Ok) {
            reject(requestErrorFromResult(result as RequestResult, 'requestToRouter failed'));
            return;
          }
          resolve(new Received((replyParts ?? []).map((part) => Message.from(part))));
        },
        0,
        timeoutMs | 0
      );
    });
  }
  replyToRouter(peerRid: BufferLike, requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
  replyToRouter(peerRid: BufferLike, requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;
  replyToRouter(peerRid: BufferLike, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    requireNative().spotReplyRouter(
      this._native,
      normalizeRoutingId(peerRid),
      requestSeq,
      Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)],
      flags | 0
    );
  }
  recvRouted(flags: RecvFlags = RecvFlags.None): Received {
    let raw;
    try {
      raw = requireNative().spotRecvRouted(this._native, flags | 0) as { sourceRid?: Buffer | null; spotRid?: Buffer | null; requestSeq?: bigint | null; parts: MessageSnapshot[] } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recvRouted failed');
    }
    if (!raw) throw lastError('recv', 'recvRouted failed');
    return new Received(raw.parts.map((part) => Message.fromSnapshot(part)), raw.sourceRid ?? raw.spotRid ?? null, raw.requestSeq ?? null);
  }
  onRoutedReceive(handler: SpotRoutedHandler): void {
    requireNative().spotRoutedHandler(this._native, (sourceRid: Buffer | null, spotRid: Buffer | null, requestSeq: bigint, parts: Buffer[]) => handler(sourceRid, spotRid, requestSeq, parts.map((part) => Message.from(part))));
  }
  onDispatchEvent(handler: SpotDispatchEventHandler): void {
    requireNative().spotDispatchEventHandler(this._native, handler);
  }
  setLinger(milliseconds: number): void { requireNative().socketSetOpt(this._native, SocketOption.LINGER, int32Buffer(milliseconds, 'milliseconds')); }
  setSendHighWaterMark(value: number): void { requireNative().socketSetOpt(this._native, SocketOption.SNDHWM, int32Buffer(value, 'value')); }
  setReceiveHighWaterMark(value: number): void { requireNative().socketSetOpt(this._native, SocketOption.RCVHWM, int32Buffer(value, 'value')); }
  setSendTimeout(milliseconds: number): void { requireNative().socketSetOpt(this._native, SocketOption.SNDTIMEO, int32Buffer(milliseconds, 'milliseconds')); }
  setReceiveTimeout(milliseconds: number): void { requireNative().socketSetOpt(this._native, SocketOption.RCVTIMEO, int32Buffer(milliseconds, 'milliseconds')); }
  setNoDrop(enabled: boolean): void { requireNative().socketSetOpt(this._native, SocketOption.XPUB_NODROP, boolBuffer(enabled)); }
  close(): void { if (this._native) { requireNative().spotDestroy(this._native); this._native = null; } }
}

function requestCallbackWrap(
  submit: () => Promise<Received> | void,
  callback?: RequestResultCallback
): Promise<Received> | void {
  const result = submit();
  if (callback) {
    (result as Promise<Received>).then(
      (reply) => callback(RequestResult.Ok, reply),
      (err: ZlinkError) => callback((err as any).result ?? RequestResult.ProtocolError)
    );
    return;
  }
  return result;
}

export class RequestDealer {
  private readonly _socket: DealerSocket;
  constructor(socket: DealerSocket) { this._socket = socket; }
  socket(): DealerSocket { return this._socket; }
  request(message: MessageLike, timeout?: number): Promise<Received>;
  request(parts: readonly MessageLike[], timeout?: number): Promise<Received>;
  request(message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  request(parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  request(payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestResultCallback | number, flags: SendFlags = SendFlags.None, timeout = 0): Promise<Received> | void {
    const normalized = normalizeMessageLikePayload(payloadOrParts);
    if (typeof callbackOrTimeout === 'function') {
      const callback = callbackOrTimeout;
      return void requireNative().dealerRequest(
        this._socket.nativeHandle(),
        Array.isArray(normalized) ? normalized : [normalized],
        timeout | 0,
        (errnum: number, replyParts: Buffer[] | null) => callback(errnum as RequestResult, replyParts ? new Received(replyParts.map((part) => Message.from(part))) : undefined)
      );
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : timeout) ?? 0;
    return new Promise<Received>((resolve, reject) => {
      requireNative().dealerRequest(
        this._socket.nativeHandle(),
        Array.isArray(normalized) ? normalized : [normalized],
        timeoutMs | 0,
        (errnum: number, replyParts: Buffer[] | null) => {
          if (errnum !== 0) {
            reject(requestErrorFromResult(errnum as RequestResult, 'request failed'));
            return;
          }
          resolve(new Received((replyParts ?? []).map((part) => Message.from(part))));
        }
      );
    });
  }
  recv(flags: RecvFlags = RecvFlags.None): Received { return this._socket.recv(flags); }
  onReceive(handler: (received: Received) => void): void {
    this._socket.onReceive((routingId, parts) => handler(new Received(parts, routingId)));
  }
  close(): void { this._socket.close(); }
}

export class RequestRouter {
  private readonly _socket: RouterSocket;
  private _dispatchTimer: ReturnType<typeof setInterval> | null = null;
  private _receiveHandler: ((received: Received) => void) | null = null;
  constructor(socket: RouterSocket) { this._socket = socket; }
  socket(): RouterSocket { return this._socket; }
  request(routingId: BufferLike, message: MessageLike, timeout?: number): Promise<Received>;
  request(routingId: BufferLike, parts: readonly MessageLike[], timeout?: number): Promise<Received>;
  request(routingId: BufferLike, message: MessageLike, callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  request(routingId: BufferLike, parts: readonly MessageLike[], callback: RequestResultCallback, flags?: SendFlags, timeout?: number): void;
  request(routingId: BufferLike, payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestResultCallback | number, flags: SendFlags = SendFlags.None, timeout = 0): Promise<Received> | void {
    const normalized = normalizeMessageLikePayload(payloadOrParts);
    if (typeof callbackOrTimeout === 'function') {
      const callback = callbackOrTimeout;
      return void requireNative().routerRequest(
        this._socket.nativeHandle(),
        normalizeRoutingId(routingId),
        Array.isArray(normalized) ? normalized : [normalized],
        timeout | 0,
        (errnum: number, replyParts: Buffer[] | null) => callback(errnum as RequestResult, replyParts ? new Received(replyParts.map((part) => Message.from(part))) : undefined)
      );
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : timeout) ?? 0;
    return new Promise<Received>((resolve, reject) => {
      requireNative().routerRequest(
        this._socket.nativeHandle(),
        normalizeRoutingId(routingId),
        Array.isArray(normalized) ? normalized : [normalized],
        timeoutMs | 0,
        (errnum: number, replyParts: Buffer[] | null) => {
          if (errnum !== 0) {
            reject(requestErrorFromResult(errnum as RequestResult, 'request failed'));
            return;
          }
          resolve(new Received((replyParts ?? []).map((part) => Message.from(part))));
        }
      );
    });
  }
  reply(routingId: BufferLike, requestSeq: bigint, message: MessageLike, flags: SendFlags = SendFlags.None): void {
    requireNative().routerReply(this._socket.nativeHandle(), normalizeRoutingId(routingId), requestSeq, [message instanceof Message ? message.toSnapshot() : normalizeBufferLike(message, 'message')], flags | 0);
  }
  replyParts(routingId: BufferLike, requestSeq: bigint, parts: readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    requireNative().routerReply(this._socket.nativeHandle(), normalizeRoutingId(routingId), requestSeq, toMessageParts(parts), flags | 0);
  }
  recv(flags: RecvFlags = RecvFlags.None): Received { return this._socket.recv(flags); }
  onReceive(handler: (received: Received) => void): void {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    this._receiveHandler = handler;
    if (!this._dispatchTimer) {
      this._dispatchTimer = setInterval(() => {
        while (this._receiveHandler) {
          try {
            const received = this._socket.recv(RecvFlags.DontWait);
            this._receiveHandler(received);
          } catch (error) {
            if (error instanceof RecvError && error.result === RecvResult.NoData) {
              break;
            }
            throw error;
          }
        }
      }, 1);
      this._dispatchTimer.unref?.();
    }
  }
  close(): void {
    if (this._dispatchTimer) {
      clearInterval(this._dispatchTimer);
      this._dispatchTimer = null;
    }
    this._receiveHandler = null;
    this._socket.close();
  }
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
  size(): number { return requireNative().pollerSize(this._native) as number; }
  wait(timeoutMs: number): any { return requireNative().pollerWait(this._native, timeoutMs | 0); }
  waitAll(events: number, timeoutMs: number): any[] { return requireNative().pollerWaitAll(this._native, events | 0, timeoutMs | 0) as any[]; }
  poll(timeoutMs: number): number[] { return requireNative().poll(this._native, timeoutMs | 0) as number[]; }
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

export function errno(): number { return readErrno(); }
export function strerror(code: number): string { return requireNative().strerror(code) as string; }
export function has(capability: string): boolean { return requireNative().has(validateCString(capability, 'capability', Number.MAX_SAFE_INTEGER)) as boolean; }
export function proxy(frontend: BaseSocket, backend: BaseSocket, capture?: BaseSocket | null): void { requireNative().proxy(frontend.nativeHandle(), backend.nativeHandle(), capture ? capture.nativeHandle() : null); }
export function proxySteerable(frontend: BaseSocket, backend: BaseSocket, capture: BaseSocket | null, control: BaseSocket): void { requireNative().proxySteerable(frontend.nativeHandle(), backend.nativeHandle(), capture ? capture.nativeHandle() : null, control.nativeHandle()); }
export function sleep(seconds: number): void { requireNative().sleep(seconds | 0); }
export function multipartClose(parts: Message[]): void { for (const part of parts) part.close(); }
