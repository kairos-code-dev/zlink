// SPDX-License-Identifier: MPL-2.0

import { Message, RoutingId, type MessageLike } from '../../contracts';
import {
  PollEventFlag,
  RidDuplicatePolicy,
  type PollEventFlagValue,
  type RidDuplicatePolicy as RidDuplicatePolicyValue
} from '../../contracts/sockets/socket_constants';
import { normalizeMessageLikePayload } from '../buffers/message_conversion';
import { normalizeRoutingId } from '../core/routing_id';
import { SocketBase } from './socket_base';
import { SocketOption } from '../options/option_mapping';

export function int32Buffer(value: number, name: string): Buffer {
  if (!Number.isInteger(value)) throw new TypeError(`${name} must be an integer`);
  if (value < -2147483648 || value > 2147483647) {
    throw new RangeError(`${name} must fit in int32`);
  }
  const buf = Buffer.allocUnsafe(4);
  buf.writeInt32LE(value, 0);
  return buf;
}

export function int64Buffer(value: bigint, name: string): Buffer {
  if (typeof value !== 'bigint') {
    throw new TypeError(`${name} must be a bigint`);
  }
  const normalized = value;
  const min = -(1n << 63n);
  const max = (1n << 63n) - 1n;
  if (normalized < min || normalized > max) {
    throw new RangeError(`${name} must fit in int64`);
  }
  const buf = Buffer.allocUnsafe(8);
  buf.writeBigInt64LE(normalized, 0);
  return buf;
}

export function boolBuffer(value: boolean): Buffer {
  const buf = Buffer.allocUnsafe(4);
  buf.writeUInt32LE(value ? 1 : 0, 0);
  return buf;
}

export function flagsToMask(events: readonly PollEventFlagValue[]): number {
  if (!Array.isArray(events)) {
    throw new TypeError('events must be an array');
  }
  return events.reduce((mask, event) => mask | (event | 0), 0);
}

export const POLL_EVENT_FLAGS = Object.freeze([
  PollEventFlag.PollIn,
  PollEventFlag.PollOut,
  PollEventFlag.PollErr,
  PollEventFlag.PollPri,
  PollEventFlag.PollCompletion
]);

export function maskToFlags(mask: number): PollEventFlagValue[] {
  const value = mask | 0;
  const flags: PollEventFlagValue[] = [];
  for (const flag of POLL_EVENT_FLAGS) {
    if ((value & flag) !== 0) flags.push(flag);
  }
  return flags;
}

export function readBoolOption(buffer: Buffer, name: string): boolean {
  if (buffer.length < 4) throw new Error(`${name} option returned an invalid payload`);
  return buffer.readUInt32LE(0) !== 0;
}

export function readInt32Option(buffer: Buffer, name: string): number {
  if (buffer.length < 4) throw new Error(`${name} option returned an invalid payload`);
  return buffer.readInt32LE(0);
}

export function readInt64Option(buffer: Buffer, name: string): bigint {
  if (buffer.length < 8) throw new Error(`${name} option returned an invalid payload`);
  return buffer.readBigInt64LE(0);
}

export function readRoutingIdOption(buffer: Buffer): RoutingId | null {
  return buffer.length === 0 ? null : RoutingId.from(buffer);
}

export function readStringOption(buffer: Buffer): string {
  const nul = buffer.indexOf(0);
  return buffer.subarray(0, nul >= 0 ? nul : buffer.length).toString();
}

export const OPTION_CREATE_TOKEN = Symbol('OptionFacade.create');

export class CommonSocketOptions {
  /** @internal */
  protected readonly _socket: SocketBase;

  /** @internal */
  protected constructor(token: symbol, socket: SocketBase) {
    if (token !== OPTION_CREATE_TOKEN) {
      throw new TypeError('socket options are created by sockets');
    }
    this._socket = socket;
  }

  /** @internal */
  static create(socket: SocketBase): CommonSocketOptions {
    return new CommonSocketOptions(OPTION_CREATE_TOKEN, socket);
  }

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
  get ridDuplicatePolicy(): RidDuplicatePolicyValue { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RID_DUPLICATE_POLICY), 'ridDuplicatePolicy') as RidDuplicatePolicyValue; }
  set ridDuplicatePolicy(value: RidDuplicatePolicyValue) { this._socket.setSockOptRaw(SocketOption.RID_DUPLICATE_POLICY, int32Buffer(value, 'ridDuplicatePolicy')); }
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
  set maxMsgSize(value: bigint) { this._socket.setSockOptRaw(SocketOption.MAXMSGSIZE, int64Buffer(value, 'maxMsgSize')); }
  get lastEndpoint(): string { return readStringOption(this._socket.getSockOptRaw(SocketOption.LAST_ENDPOINT)); }
  get backlog(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.BACKLOG), 'backlog'); }
  set backlog(value: number) { this._socket.setSockOptRaw(SocketOption.BACKLOG, int32Buffer(value, 'backlog')); }
  get reconnectInterval(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RECONNECT_IVL), 'reconnectInterval'); }
  set reconnectInterval(value: number) { this._socket.setSockOptRaw(SocketOption.RECONNECT_IVL, int32Buffer(value, 'reconnectInterval')); }
  get reconnectIntervalMax(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RECONNECT_IVL_MAX), 'reconnectIntervalMax'); }
  set reconnectIntervalMax(value: number) { this._socket.setSockOptRaw(SocketOption.RECONNECT_IVL_MAX, int32Buffer(value, 'reconnectIntervalMax')); }
  get submitRetryMode(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.SUBMIT_RETRY_MODE), 'submitRetryMode'); }
  set submitRetryMode(value: number) { this._socket.setSockOptRaw(SocketOption.SUBMIT_RETRY_MODE, int32Buffer(value, 'submitRetryMode')); }
  get submitRetryTimeout(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.SUBMIT_RETRY_TIMEOUT), 'submitRetryTimeout'); }
  set submitRetryTimeout(value: number) { this._socket.setSockOptRaw(SocketOption.SUBMIT_RETRY_TIMEOUT, int32Buffer(value, 'submitRetryTimeout')); }
  get submitRetryAttempts(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.SUBMIT_RETRY_ATTEMPTS), 'submitRetryAttempts'); }
  set submitRetryAttempts(value: number) { this._socket.setSockOptRaw(SocketOption.SUBMIT_RETRY_ATTEMPTS, int32Buffer(value, 'submitRetryAttempts')); }
}

export class DealerSocketOptions extends CommonSocketOptions {
  /** @internal */
  private constructor(token: symbol, socket: SocketBase) { super(token, socket); }
  /** @internal */
  static create(socket: SocketBase): DealerSocketOptions {
    return new DealerSocketOptions(OPTION_CREATE_TOKEN, socket);
  }
  set probe(value: boolean) { this._socket.setSockOptRaw(SocketOption.DEALER_PROBE, boolBuffer(value)); }
  set requestTimeout(value: number) { this._socket.setSockOptRaw(SocketOption.DEALER_REQUEST_TIMEOUT_MS, int32Buffer(value, 'requestTimeout')); }
  set peerWeight(value: number) { this._socket.setSockOptRaw(SocketOption.DEALER_WEIGHT, int32Buffer(value, 'peerWeight')); }
}

export class RouterSocketOptions extends CommonSocketOptions {
  /** @internal */
  private constructor(token: symbol, socket: SocketBase) { super(token, socket); }
  /** @internal */
  static create(socket: SocketBase): RouterSocketOptions {
    return new RouterSocketOptions(OPTION_CREATE_TOKEN, socket);
  }
  get mandatory(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.ROUTER_MANDATORY), 'mandatory'); }
  set mandatory(value: boolean) { this._socket.setSockOptRaw(SocketOption.ROUTER_MANDATORY, boolBuffer(value)); }
  get handover(): boolean { return this.ridDuplicatePolicy === RidDuplicatePolicy.Handover; }
  set handover(value: boolean) { this.ridDuplicatePolicy = value ? RidDuplicatePolicy.Handover : RidDuplicatePolicy.Reject; }
  get probe(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.PROBE_ROUTER), 'probe'); }
  set probe(value: boolean) { this._socket.setSockOptRaw(SocketOption.PROBE_ROUTER, boolBuffer(value)); }
  get connectRoutingId(): RoutingId | null { return readRoutingIdOption(this._socket.getSockOptRaw(SocketOption.CONNECT_ROUTING_ID)); }
  setConnectRoutingId(routingId: RoutingId): void {
    this._socket.setSockOptRaw(
      SocketOption.CONNECT_ROUTING_ID,
      normalizeRoutingId(routingId, 'routingId')
    );
  }
  get requestTimeout(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.ROUTER_REQUEST_TIMEOUT_MS), 'requestTimeout'); }
  set requestTimeout(value: number) { this._socket.setSockOptRaw(SocketOption.ROUTER_REQUEST_TIMEOUT_MS, int32Buffer(value, 'requestTimeout')); }
  get peerWeight(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.ROUTER_WEIGHT), 'peerWeight'); }
  set peerWeight(value: number) { this._socket.setSockOptRaw(SocketOption.ROUTER_WEIGHT, int32Buffer(value, 'peerWeight')); }
}

export class StreamSocketOptions extends CommonSocketOptions {
  /** @internal */
  private constructor(token: symbol, socket: SocketBase) { super(token, socket); }
  /** @internal */
  static create(socket: SocketBase): StreamSocketOptions {
    return new StreamSocketOptions(OPTION_CREATE_TOKEN, socket);
  }
  get notify(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.STREAM_NOTIFY), 'notify'); }
  set notify(value: boolean) { this._socket.setSockOptRaw(SocketOption.STREAM_NOTIFY, boolBuffer(value)); }
}

export class PubSocketOptions extends CommonSocketOptions {
  private _welcomeMessage = Buffer.alloc(0);

  /** @internal */
  private constructor(token: symbol, socket: SocketBase) { super(token, socket); }
  /** @internal */
  static create(socket: SocketBase): PubSocketOptions {
    return new PubSocketOptions(OPTION_CREATE_TOKEN, socket);
  }
  get verbose(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.XPUB_VERBOSE), 'verbose'); }
  set verbose(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_VERBOSE, boolBuffer(value));
    this._socket.setSockOptRaw(SocketOption.XPUB_VERBOSER, boolBuffer(false));
  }
  get verboser(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.XPUB_VERBOSER), 'verboser'); }
  set verboser(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_VERBOSE, boolBuffer(value));
    this._socket.setSockOptRaw(SocketOption.XPUB_VERBOSER, boolBuffer(value));
  }
  get noDrop(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.XPUB_NODROP), 'noDrop'); }
  set noDrop(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_NODROP, boolBuffer(value));
  }
  get manual(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.XPUB_MANUAL), 'manual'); }
  set manual(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_MANUAL, boolBuffer(value));
  }
  get manualLastValue(): boolean { return readBoolOption(this._socket.getSockOptRaw(SocketOption.XPUB_MANUAL_LAST_VALUE), 'manualLastValue'); }
  set manualLastValue(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_MANUAL, boolBuffer(value));
    this._socket.setSockOptRaw(SocketOption.XPUB_MANUAL_LAST_VALUE, boolBuffer(value));
  }
  get topicsCount(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.XPUB_TOPICS_COUNT), 'topicsCount'); }
  welcomeMessage(): Message { return Message.from(this._welcomeMessage); }
  setWelcomeMessage(message: MessageLike): void {
    const payload = normalizeMessageLikePayload(message);
    if (Array.isArray(payload)) throw new TypeError('welcome payload must contain one frame');
    const data = Buffer.isBuffer(payload) ? payload : payload.data;
    this._socket.setSockOptRaw(SocketOption.XPUB_WELCOME_MSG, data);
    this._welcomeMessage = Buffer.from(data);
  }
  approveSubscribe(routingId: RoutingId): void {
    this._socket.setSockOptRaw(SocketOption.XPUB_APPROVE_SUBSCRIBE, normalizeRoutingId(routingId));
  }
  rejectSubscribe(routingId: RoutingId): void {
    this._socket.setSockOptRaw(SocketOption.XPUB_REJECT_SUBSCRIBE, normalizeRoutingId(routingId));
  }
}

export class SubSocketOptions extends CommonSocketOptions {
  /** @internal */
  private constructor(token: symbol, socket: SocketBase) { super(token, socket); }
  /** @internal */
  static create(socket: SocketBase): SubSocketOptions {
    return new SubSocketOptions(OPTION_CREATE_TOKEN, socket);
  }
  get topicsCount(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.SUB_TOPICS_COUNT), 'topicsCount'); }
}
