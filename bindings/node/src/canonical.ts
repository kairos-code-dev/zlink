// SPDX-License-Identifier: MPL-2.0

import { randomBytes } from 'node:crypto';
import { Worker } from 'node:worker_threads';
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
import {
  SocketType as NativeSocketType, SocketOption, SendFlags, RecvFlags, RidDuplicatePolicy,
  PollEventFlag, type PollEventFlagValue, type RidDuplicatePolicy as RidDuplicatePolicyValue
} from './socket/constants';
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
export type { PollEventFlagValue, RidDuplicatePolicy as RidDuplicatePolicyValue } from './socket/constants';
export {
  Message,
  Received,
  RoutingId,
  TopicMessage,
  SubscriptionEvent,
  SendFlags,
  RecvFlags,
  PollEventFlag,
  RidDuplicatePolicy,
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
  BLOCKY: 10,
  AUTO_HWM_ENABLE: 12,
  AUTO_HWM_RECALC_DEBOUNCE_MS: 14,
  AUTO_HWM_PROFILE: 17
} as const);

export const AutoHwmProfile = Object.freeze({
  Compact: 0,
  LowLatency: 1,
  Balanced: 2,
  Throughput: 3
} as const);
export type AutoHwmProfileValue = typeof AutoHwmProfile[keyof typeof AutoHwmProfile];

const SpotNodeOption = Object.freeze({
  ROUTER_HWM_PROFILE: 0x360E,
  ROUTER_HWM: 0x360F,
  PUBSUB_HWM_PROFILE: 0x3610,
  PUBSUB_HWM: 0x3611,
  DISPATCH_WORKERS_MIN: 0x3612,
  DISPATCH_WORKERS_MAX: 0x3613
} as const);

const SpotOption = Object.freeze({
  REQUEST_TIMEOUT_MS: 0x3701
} as const);

export const MonitorSourceKind = Object.freeze({ Socket: 1, SpotPub: 3, SpotSub: 4 } as const);
export type MonitorSourceKindValue = typeof MonitorSourceKind[keyof typeof MonitorSourceKind];
const MonitorState = Object.freeze({
  READY: 1 << 0, BOUND_READY: 1 << 1, CLOSED: 1 << 3
} as const);
const MonitorSnapshotDetail = Object.freeze({
  SND_PENDING_MSGS: 1 << 1, RCV_PENDING_MSGS: 1 << 2
} as const);

export const SpotDispatchEvent = Object.freeze({
  SubscribeReadable: 1,
  RoutedReadable: 2,
  TimerReadable: 3,
  ChannelReplyReadable: 4,
  ActorReadable: 5,
  ActorJoinReadable: 6
} as const);
export type SpotDispatchEvent = typeof SpotDispatchEvent[keyof typeof SpotDispatchEvent];

export const SpotDispatchSubjectKind = Object.freeze({
  Spot: 1,
  Timer: 2,
  ChannelDealer: 3,
  Actor: 4
} as const);
export type SpotDispatchSubjectKind = typeof SpotDispatchSubjectKind[keyof typeof SpotDispatchSubjectKind];

export const ActorCreateStatus = Object.freeze({
  Created: 1,
  Existing: 2
} as const);
export type ActorCreateStatus = typeof ActorCreateStatus[keyof typeof ActorCreateStatus];

export const ActorAdmissionResult = Object.freeze({
  Accept: 1,
  Reject: 2
} as const);
export type ActorAdmissionResult = typeof ActorAdmissionResult[keyof typeof ActorAdmissionResult];

export const MonitorEventType = Object.freeze({
  Connected: 0x0001,
  ConnectDelayed: 0x0002,
  ConnectRetried: 0x0004,
  Listening: 0x0008,
  BindFailed: 0x0010,
  Accepted: 0x0020,
  AcceptFailed: 0x0040,
  Closed: 0x0080,
  CloseFailed: 0x0100,
  Disconnected: 0x0200,
  MonitorStopped: 0x0400,
  HandshakeFailedNoDetail: 0x0800,
  ConnectionReady: 0x1000,
  HandshakeFailedProtocol: 0x2000,
  HandshakeFailedAuth: 0x4000,
  PeerWeightChanged: 0x8000
} as const);
export type MonitorEventType = typeof MonitorEventType[keyof typeof MonitorEventType];

export interface MonitorSnapshot {
  readonly sourceKind: MonitorSourceKindValue;
  readonly stateFlags: number;
  readonly detailFlags: number;
  readonly sndPendingMsgs: bigint;
  readonly rcvPendingMsgs: bigint;
  readonly autoHwmEnabled: boolean;
  readonly autoHwmProfile: number;
  readonly autoHwmRole: number;
  readonly autoHwmPolicyClass: number;
  readonly autoHwmUnitBudgetBytes: bigint;
  readonly autoHwmSizeCap: number;
  readonly autoHwmSocketMessageSlots: bigint;
  readonly autoHwmEffectiveMessageBytes: bigint;
  readonly autoHwmAppliedSndHwm: number;
  readonly autoHwmAppliedRcvHwm: number;
  readonly autoHwmEffectiveSndBuf: number;
  readonly autoHwmEffectiveRcvBuf: number;
  readonly autoHwmLastRecalcMs: bigint;
  readonly autoHwmLastRecalcReason: number;
  readonly autoHwmSendBlockedRatioPpm: number;
  readonly autoHwmDeferredSndHwm: number;
  readonly autoHwmDeferredRcvHwm: number;
  isReady(): boolean;
}

interface MonitorSnapshotRaw {
  sourceKind: number;
  stateFlags: number;
  detailFlags: number;
  sndPendingMsgs: number | bigint;
  rcvPendingMsgs: number | bigint;
  autoHwmEnabled: boolean;
  autoHwmProfile: number;
  autoHwmRole: number;
  autoHwmPolicyClass: number;
  autoHwmUnitBudgetBytes: number | bigint;
  autoHwmSizeCap: number;
  autoHwmSocketMessageSlots: number | bigint;
  autoHwmEffectiveMessageBytes: number | bigint;
  autoHwmAppliedSndHwm: number;
  autoHwmAppliedRcvHwm: number;
  autoHwmEffectiveSndBuf?: number;
  autoHwmEffectiveRcvBuf?: number;
  autoHwmLastRecalcMs: number | bigint;
  autoHwmLastRecalcReason: number;
  autoHwmSendBlockedRatioPpm: number;
  autoHwmDeferredSndHwm: number;
  autoHwmDeferredRcvHwm: number;
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

const MONITOR_EVENT_CREATE_TOKEN = Symbol('MonitorEvent.create');

export class MonitorEvent {
  readonly event: MonitorEventType;
  readonly value: number;
  readonly routingId: RoutingId | null;
  readonly localAddr: string;
  readonly remoteAddr: string;

  private constructor(token: symbol, raw: MonitorEventValueRaw) {
    if (token !== MONITOR_EVENT_CREATE_TOKEN) {
      throw new TypeError('MonitorEvent values are created by monitor recv operations');
    }
    this.event = raw.event as MonitorEventType;
    this.value = raw.value;
    this.routingId = wrapRoutingId(raw.routingId ?? null);
    this.localAddr = raw.localAddr ?? raw.local ?? '';
    this.remoteAddr = raw.remoteAddr ?? raw.remote ?? '';
  }

  /** @internal */
  static create(raw: MonitorEventValueRaw): MonitorEvent {
    return new MonitorEvent(MONITOR_EVENT_CREATE_TOKEN, raw);
  }
}

Object.freeze(MonitorEvent);

export const AutoConnectType = Object.freeze({
  Invalid: 0, RouteMesh: 1, ClientServer: 2, DealerMesh: 3, Fanout: 4, SpotMesh: 5
} as const);
export type AutoConnectType = typeof AutoConnectType[keyof typeof AutoConnectType];
export const ServiceRole = Object.freeze({
  Invalid: 0, Spot: 2, Router: 3, Dealer: 4, Pub: 5, Sub: 6
} as const);
export type ServiceRoleValue = typeof ServiceRole[keyof typeof ServiceRole];
export const ServiceKind = Object.freeze({
  Discovery: 1, SpotSub: 3, SpotPub: 4, Socket: 5
} as const);
export type ServiceKindValue = typeof ServiceKind[keyof typeof ServiceKind];
export const SpotRole = Object.freeze({ Pub: 1, Sub: 2 } as const);
export type SpotRoleValue = typeof SpotRole[keyof typeof SpotRole];
export const SpotPeerSource = Object.freeze({ Manual: 1, Discovery: 2, Mixed: 3 } as const);
export type SpotPeerSourceValue = typeof SpotPeerSource[keyof typeof SpotPeerSource];
export const SpotPeerState = Object.freeze({ Configured: 1, Connecting: 2, Connected: 3 } as const);
export type SpotPeerStateValue = typeof SpotPeerState[keyof typeof SpotPeerState];
export const SpotNodeState = Object.freeze({ Idle: 1, Connecting: 2, PartialReady: 3, Ready: 4, Error: 5 } as const);
export type SpotNodeStateValue = typeof SpotNodeState[keyof typeof SpotNodeState];
export const SpotNodeMode = Object.freeze({ PubSub: 1, Routed: 2, All: 3 } as const);
export const SpotNodeSocketOwner = Object.freeze({ Any: 0, Node: 1, Spot: 2 } as const);
export type SpotNodeSocketOwnerValue = typeof SpotNodeSocketOwner[keyof typeof SpotNodeSocketOwner];
export const SocketType = Object.freeze({
  Any: 0,
  Pair: 0x1001,
  Pub: 0x1002,
  Sub: 0x1003,
  Dealer: 0x1004,
  Router: 0x1005,
  XPub: 0x1006,
  XSub: 0x1007,
  Stream: 0x1008
} as const);
export type SocketTypeValue = typeof SocketType[keyof typeof SocketType];
export const RegistryState = Object.freeze({ Idle: 1, Active: 2, Degraded: 3, Error: 4 } as const);
export type RegistryStateValue = typeof RegistryState[keyof typeof RegistryState];
export const TopologySource = Object.freeze({ Manual: 1, Discovery: 2, Registry: 3 } as const);
export type TopologySourceValue = typeof TopologySource[keyof typeof TopologySource];
export const TopologyState = Object.freeze({ Discovered: 1, Connecting: 2, Ready: 3, Lost: 4, Error: 5, Stopped: 6 } as const);
export type TopologyStateValue = typeof TopologyState[keyof typeof TopologyState];
export type SpotNodeModeValue = typeof SpotNodeMode[keyof typeof SpotNodeMode];

export interface MemberPeerEntry {
  readonly autoConnectType: AutoConnectType;
  readonly serviceRole: ServiceRoleValue;
  readonly channelName: string;
  readonly endpoint: string;
  readonly routingId: RoutingId;
  readonly weight: number;
  readonly value: bigint;
}

export interface RegistryTopologyEntry {
  readonly autoConnectType: AutoConnectType;
  readonly routingId: RoutingId;
  readonly serviceKind: ServiceKindValue;
  readonly serviceRole: ServiceRoleValue;
  readonly channelName: string;
  readonly endpoint: string;
  readonly source: TopologySourceValue;
  readonly state: TopologyStateValue;
  readonly desiredCount: number;
  readonly readyCount: number;
  readonly errorCode: number;
  readonly lastReportedMs: bigint;
}

export interface RegistryServiceSummaryEntry {
  readonly autoConnectType: AutoConnectType;
  readonly serviceRole: ServiceRoleValue;
  readonly channelName: string;
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
  readonly state: RegistryStateValue;
  readonly topologyEntryCount: number;
  readonly peerRegistryCount: number;
  readonly connectedPeerRegistryCount: number;
  readonly listSeq: bigint;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

export interface SpotNodeStatus {
  readonly channelName: string;
  readonly localEndpoint: string;
  readonly nodeRoutingId: RoutingId;
  readonly state: SpotNodeStateValue;
  readonly configuredPeerCount: number;
  readonly activePeerCount: number;
  readonly connectedPeerCount: number;
  readonly subjectCount: number;
  readonly readySubjectCount: number;
  readonly disconnectedSubTargetCount: number;
  readonly disconnectedRoutedTargetCount: number;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

export interface SpotNodePeerEntry {
  readonly channelName: string;
  readonly localEndpoint: string;
  readonly peerEndpoint: string;
  readonly source: SpotPeerSourceValue;
  readonly state: SpotPeerStateValue;
  readonly weight: number;
  readonly connectedSinceMs: bigint;
  readonly lastChangedMs: bigint;
}

export interface SpotNodeSubjectEntry {
  readonly role: SpotRoleValue;
  readonly subject: string;
  readonly subjectKind: number;
  readonly readyPeerCount: number;
  readonly activePeerCount: number;
  readonly lastChangedMs: bigint;
}

export interface SpotNodeSocketSnapshotFilter {
  readonly owner?: SpotNodeSocketOwnerValue;
  readonly socketType?: SocketTypeValue;
  readonly socketName?: string;
}

export interface SpotNodeSocketSnapshotEntry {
  readonly owner: SpotNodeSocketOwnerValue;
  readonly ownerId: bigint;
  readonly ownerName: string;
  readonly socketName: string;
  readonly socketType: SocketTypeValue;
  readonly autoHwmVisible: boolean;
  readonly snapshot: MonitorSnapshot;
}

export interface RegistryServiceSummaryFilter {
  readonly autoConnectType?: AutoConnectType;
  readonly serviceRole?: ServiceRoleValue;
  readonly channelName?: string;
}

export interface RegistryTopologyFilter {
  readonly autoConnectType?: AutoConnectType;
  readonly serviceKind?: ServiceKindValue;
  readonly serviceRole?: ServiceRoleValue;
  readonly channelName?: string;
  readonly routingId?: RoutingId;
  readonly state?: TopologyStateValue;
  readonly source?: TopologySourceValue;
}

export interface SpotNodePeerFilter {
  readonly peerEndpoint?: string;
  readonly source?: SpotPeerSourceValue;
  readonly state?: SpotPeerStateValue;
}

export interface SpotNodeSubjectFilter {
  readonly role?: SpotRoleValue;
  readonly subject?: string;
  readonly subjectKind?: number;
}

export interface SubscriptionEntry {
  readonly filter: string;
  readonly isPattern: boolean;
}

/** @internal */
export type SocketRecvHandler = (message: Received) => void;
/** @internal */
export type SocketSubscribeHandler = (message: TopicMessage) => void;
export type SocketSendReadyHandler = () => void;
export type StreamPacketHandler = (sourceRid: RoutingId, header: Message, body: Message) => void;
export type SocketMonitorHandler = (event: MonitorEvent) => void;
/** @internal */
export type SpotSubHandler = SocketSubscribeHandler;
export type SpotSendReadyHandler = () => void;
export type SpotRoutedHandler = (message: Received) => void;
export interface ActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}
export interface ActorCreateResult {
  readonly status: ActorCreateStatus;
  readonly actor: ActorRef;
}
export interface ActorRoute {
  readonly actor: ActorRef;
  readonly joined: boolean;
  readonly joinedSpotRid: RoutingId | null;
}
export interface ActorRecvInfo {
  readonly actor: ActorRef;
  readonly sourceNodeRid: RoutingId;
  readonly sourceSessionRid: RoutingId;
  readonly flags: number;
}
export interface ActorJoinInfo {
  readonly sourceActor: ActorRef;
  readonly targetActor: ActorRef;
  readonly sourceNodeRid: RoutingId;
  readonly sourceSpotRid: RoutingId;
  readonly targetNodeRid: RoutingId;
  readonly targetSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}
export interface ActorPart {
  readonly info: ActorRecvInfo;
  readonly message: Message;
  readonly more: boolean;
}
export interface ActorJoinRequest {
  readonly info: ActorJoinInfo;
  readonly message: Message;
}
export interface SpotNodeSpotEntry {
  readonly spotRid: RoutingId;
  readonly dispatchHandlerAttached: boolean;
  readonly joinedActorCount: number;
  readonly pendingActorJoinCount: number;
  readonly routeSynced: boolean;
  readonly lastChangedMs: bigint;
}
export interface SpotNodeActorEntry {
  readonly actor: ActorRef;
  readonly joined: boolean;
  readonly joinedSpotRid: RoutingId | null;
  readonly routeSynced: boolean;
  readonly pendingMessageCount: number;
  readonly lastChangedMs: bigint;
}
export type ActorAdmissionHandler =
  (actorId: string, message: Message) => ActorAdmissionResult;
export interface SpotDispatchInfo {
  readonly event: SpotDispatchEvent;
  readonly subjectKind: SpotDispatchSubjectKind;
  readonly timer: Timer | null;
  readonly actorRef: ActorRef | null;
  recvActorPart(flags?: RecvFlags): ActorPart | null;
}
export type SpotDispatchEventHandler = (info: SpotDispatchInfo) => void;
export type TimerHandler = (timer: Timer, fireCount: bigint) => void;
export type RequestCallback = (result: RequestResult, parts: readonly Message[]) => void;

export interface SendOp {
  message(message: MessageLike): SendSubmitOp;
}

export interface SendSubmitOp {
  message(message: MessageLike): SendSubmitOp;
  flags(flags: SendFlags): SendSubmitOp;
  submit(): boolean;
}

export interface RequestOp {
  message(message: MessageLike): RequestSubmitOp;
}

export interface RequestSubmitOp {
  message(message: MessageLike): RequestSubmitOp;
  timeout(timeoutMs: number): RequestSubmitOp;
  flags(flags: SendFlags): RequestCallbackSubmitOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: RequestCallback): boolean;
}

export interface RequestCallbackSubmitOp {
  message(message: MessageLike): RequestCallbackSubmitOp;
  timeout(timeoutMs: number): RequestCallbackSubmitOp;
  flags(flags: SendFlags): RequestCallbackSubmitOp;
  submit(callback: RequestCallback): boolean;
}

export interface ReplyOp {
  message(message: MessageLike): ReplySubmitOp;
}

export interface ReplySubmitOp {
  message(message: MessageLike): ReplySubmitOp;
  flags(flags: SendFlags): ReplySubmitOp;
  submit(): void;
}

function readErrno(): number {
  const native = requireNative();
  return typeof native.errno === 'function' ? native.errno() as number : 0;
}

type NativeErrorCategory =
  | 'submit'
  | 'request'
  | 'recv'
  | 'handler'
  | 'close'
  | 'bind'
  | 'connect'
  | 'config';

function nativeErrorMessage(error: unknown, fallbackMessage: string): string {
  return error instanceof Error && error.message ? error.message : fallbackMessage;
}

function lastError(category: NativeErrorCategory, message: string): ZlinkError {
  return createError(category, readErrno(), message);
}

function nativeCall<T>(category: NativeErrorCategory, fallbackMessage: string, fn: () => T): T {
  try {
    return fn();
  } catch (error) {
    throw createError(category, readErrno(), nativeErrorMessage(error, fallbackMessage));
  }
}

function bindCall<T>(fallbackMessage: string, fn: () => T): T {
  return nativeCall('bind', fallbackMessage, fn);
}

function connectCall<T>(fallbackMessage: string, fn: () => T): T {
  return nativeCall('connect', fallbackMessage, fn);
}

function configCall<T>(fallbackMessage: string, fn: () => T): T {
  return nativeCall('config', fallbackMessage, fn);
}

function handlerCall<T>(fallbackMessage: string, fn: () => T): T {
  return nativeCall('handler', fallbackMessage, fn);
}

function closeCall<T>(fallbackMessage: string, fn: () => T): T {
  return nativeCall('close', fallbackMessage, fn);
}

function recvNativeError(error: unknown, flags: RecvFlags, fallbackMessage: string): RecvError {
  const message = nativeErrorMessage(error, fallbackMessage);
  if ((flags & RecvFlags.DontWait) !== 0 && /Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
    return new RecvError(RecvResult.NoData, readErrno(), message);
  }
  return createError('recv', readErrno(), message) as RecvError;
}

function submitNativeError(error: unknown, flags: SendFlags, fallbackMessage: string): SubmitError {
  const message = nativeErrorMessage(error, fallbackMessage);
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

function remoteActorRef(targetNodeRid: RoutingId, actorId: string): ActorRef {
  return actorRefFromRaw(
    requireNative().remoteActorGetRef(
      normalizeRoutingId(targetNodeRid, 'targetNodeRid'),
      validateCString(actorId, 'actorId', 255)
    ) as any
  );
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

function toOwnedMessage(message: MessageLike): Message {
  return message instanceof Message ? message : Message.from(message);
}

function messageFromNativeBuffer(buffer: Buffer | null | undefined): Message {
  return Message.fromSnapshot({ data: buffer ?? Buffer.alloc(0) });
}

function messagesFromNativeBuffers(buffers: readonly Buffer[] | null | undefined): Message[] {
  return (buffers ?? []).map((buffer) => messageFromNativeBuffer(buffer));
}

function wrapRoutingId(routingId: Buffer | Uint8Array | null | undefined): RoutingId | null {
  if (!routingId || routingId.length === 0) {
    return null;
  }
  return RoutingId.fromBytes(routingId);
}

type RequestProgressFn = (handle: unknown) => void;

interface RequestProgressState {
  refCount: number;
  pump: RequestProgressFn;
  interval: NodeJS.Timeout;
}

const requestProgressByHandle = new Map<unknown, RequestProgressState>();

function startRequestProgress(handle: unknown, pump: RequestProgressFn): () => void {
  const existing = requestProgressByHandle.get(handle);
  if (existing) {
    existing.refCount += 1;
    return () => releaseRequestProgress(handle);
  }

  const state: RequestProgressState = {
    refCount: 1,
    pump,
    interval: setInterval(() => {
      const current = requestProgressByHandle.get(handle);
      if (!current) {
        return;
      }
      try {
        current.pump(handle);
      } catch {
        // Progress calls are best-effort. Completion still arrives through the native callback.
      }
    }, 1)
  };
  requestProgressByHandle.set(handle, state);
  return () => releaseRequestProgress(handle);
}

function releaseRequestProgress(handle: unknown): void {
  const state = requestProgressByHandle.get(handle);
  if (!state) {
    return;
  }
  state.refCount -= 1;
  if (state.refCount > 0) {
    return;
  }
  clearInterval(state.interval);
  requestProgressByHandle.delete(handle);
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
    sourceKind: raw.sourceKind as MonitorSourceKindValue,
    stateFlags: raw.stateFlags,
    detailFlags: raw.detailFlags,
    sndPendingMsgs: BigInt(raw.sndPendingMsgs),
    rcvPendingMsgs: BigInt(raw.rcvPendingMsgs),
    autoHwmEnabled: raw.autoHwmEnabled,
    autoHwmProfile: raw.autoHwmProfile,
    autoHwmRole: raw.autoHwmRole,
    autoHwmPolicyClass: raw.autoHwmPolicyClass,
    autoHwmUnitBudgetBytes: BigInt(raw.autoHwmUnitBudgetBytes),
    autoHwmSizeCap: raw.autoHwmSizeCap,
    autoHwmSocketMessageSlots: BigInt(raw.autoHwmSocketMessageSlots),
    autoHwmEffectiveMessageBytes: BigInt(raw.autoHwmEffectiveMessageBytes),
    autoHwmAppliedSndHwm: raw.autoHwmAppliedSndHwm,
    autoHwmAppliedRcvHwm: raw.autoHwmAppliedRcvHwm,
    autoHwmEffectiveSndBuf: raw.autoHwmEffectiveSndBuf ?? 0,
    autoHwmEffectiveRcvBuf: raw.autoHwmEffectiveRcvBuf ?? 0,
    autoHwmLastRecalcMs: BigInt(raw.autoHwmLastRecalcMs),
    autoHwmLastRecalcReason: raw.autoHwmLastRecalcReason,
    autoHwmSendBlockedRatioPpm: raw.autoHwmSendBlockedRatioPpm,
    autoHwmDeferredSndHwm: raw.autoHwmDeferredSndHwm,
    autoHwmDeferredRcvHwm: raw.autoHwmDeferredRcvHwm,
    isReady(): boolean {
      return (this.stateFlags & MonitorState.READY) !== 0;
    }
  };
}

function mapMemberPeerEntry(entry: {
  autoConnectType: number;
  serviceRole: number;
  channelName: string;
  endpoint: string;
  routingId: Buffer;
  weight: number;
  value: number | bigint;
}): MemberPeerEntry {
  return {
    autoConnectType: entry.autoConnectType as AutoConnectType,
    serviceRole: entry.serviceRole as ServiceRoleValue,
    channelName: entry.channelName,
    endpoint: entry.endpoint,
    routingId: RoutingId.fromBytes(entry.routingId),
    weight: entry.weight,
    value: BigInt(entry.value)
  };
}

function mapRegistryTopologyEntry(entry: {
  autoConnectType: number;
  routingId: Buffer;
  serviceKind: number;
  serviceRole: number;
  channelName: string;
  endpoint: string;
  source: number;
  state: number;
  desiredCount: number;
  readyCount: number;
  errorCode: number;
  lastReportedMs: number | bigint;
}): RegistryTopologyEntry {
  return {
    autoConnectType: entry.autoConnectType as AutoConnectType,
    routingId: RoutingId.fromBytes(entry.routingId),
    serviceKind: entry.serviceKind as ServiceKindValue,
    serviceRole: entry.serviceRole as ServiceRoleValue,
    channelName: entry.channelName,
    endpoint: entry.endpoint,
    source: entry.source as TopologySourceValue,
    state: entry.state as TopologyStateValue,
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
    state: entry.state as RegistryStateValue,
    topologyEntryCount: entry.topologyEntryCount,
    peerRegistryCount: entry.peerRegistryCount,
    connectedPeerRegistryCount: entry.connectedPeerRegistryCount,
    listSeq: BigInt(entry.listSeq),
    lastError: entry.lastError,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

function mapRegistryServiceSummaryEntry(entry: {
  autoConnectType: number;
  serviceRole: number;
  channelName: string;
  totalCount: number;
  connectingCount: number;
  readyCount: number;
  errorCount: number;
  stoppedCount: number;
  lastReportedMs: number | bigint;
}): RegistryServiceSummaryEntry {
  return {
    autoConnectType: entry.autoConnectType as AutoConnectType,
    serviceRole: entry.serviceRole as ServiceRoleValue,
    channelName: entry.channelName,
    totalCount: entry.totalCount,
    connectingCount: entry.connectingCount,
    readyCount: entry.readyCount,
    errorCount: entry.errorCount,
    stoppedCount: entry.stoppedCount,
    lastReportedMs: BigInt(entry.lastReportedMs)
  };
}

function mapSpotNodeStatus(entry: {
  channelName: string;
  localEndpoint: string;
  nodeRoutingId?: Buffer | null;
  state: number;
  configuredPeerCount: number;
  activePeerCount: number;
  connectedPeerCount: number;
  subjectCount: number;
  readySubjectCount: number;
  disconnectedSubTargetCount?: number;
  disconnectedRoutedTargetCount?: number;
  lastError: number;
  lastChangedMs: number | bigint;
}, fallbackRoutingId: RoutingId): SpotNodeStatus {
  const nodeRoutingId = entry.nodeRoutingId
    ? RoutingId.fromBytes(entry.nodeRoutingId)
    : fallbackRoutingId;
  return {
    channelName: entry.channelName,
    localEndpoint: entry.localEndpoint,
    nodeRoutingId,
    state: entry.state as SpotNodeStateValue,
    configuredPeerCount: entry.configuredPeerCount,
    activePeerCount: entry.activePeerCount,
    connectedPeerCount: entry.connectedPeerCount,
    subjectCount: entry.subjectCount,
    readySubjectCount: entry.readySubjectCount,
    disconnectedSubTargetCount: entry.disconnectedSubTargetCount ?? 0,
    disconnectedRoutedTargetCount: entry.disconnectedRoutedTargetCount ?? 0,
    lastError: entry.lastError,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

function mapSpotNodePeerEntry(entry: {
  channelName: string;
  localEndpoint: string;
  peerEndpoint: string;
  source: number;
  state: number;
  weight: number;
  connectedSinceMs: number | bigint;
  lastChangedMs: number | bigint;
}): SpotNodePeerEntry {
  return {
    channelName: entry.channelName,
    localEndpoint: entry.localEndpoint,
    peerEndpoint: entry.peerEndpoint,
    source: entry.source as SpotPeerSourceValue,
    state: entry.state as SpotPeerStateValue,
    weight: entry.weight,
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
    role: entry.role as SpotRoleValue,
    subject: entry.subject,
    subjectKind: entry.subjectKind,
    readyPeerCount: entry.readyPeerCount,
    activePeerCount: entry.activePeerCount,
    lastChangedMs: BigInt(entry.lastChangedMs)
  };
}

function actorRefFromRaw(raw: { nodeRid: Buffer; actorId: string; generation: bigint | number }): ActorRef {
  const generation = BigInt(raw.generation);
  return Object.freeze({
    nodeRid: RoutingId.fromBytes(raw.nodeRid),
    actorId: raw.actorId,
    generation
  });
}

function actorRefToRaw(actor: ActorRef): { nodeRid: Buffer; actorId: string; generation: bigint } {
  return {
    nodeRid: normalizeRoutingId(actor.nodeRid, 'actor.nodeRid'),
    actorId: validateCString(actor.actorId, 'actor.actorId', 255),
    generation: BigInt(actor.generation)
  };
}

function actorRecvInfoFromRaw(raw: {
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  sourceNodeRid: Buffer;
  sourceSessionRid: Buffer;
  flags: number;
}): ActorRecvInfo {
  return {
    actor: actorRefFromRaw(raw.actor),
    sourceNodeRid: RoutingId.fromBytes(raw.sourceNodeRid),
    sourceSessionRid: RoutingId.fromBytes(raw.sourceSessionRid),
    flags: raw.flags
  };
}

function actorPartFromRaw(raw: {
  info: {
    actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
    sourceNodeRid: Buffer;
    sourceSessionRid: Buffer;
    flags: number;
  };
  message: Buffer;
  more: boolean;
}): ActorPart {
  return {
    info: actorRecvInfoFromRaw(raw.info),
    message: messageFromNativeBuffer(raw.message),
    more: Boolean(raw.more)
  };
}

const actorJoinRequestHandles = new WeakMap<ActorJoinInfo, bigint>();

function actorJoinInfoFromRaw(raw: {
  actor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  sourceActor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  targetActor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  sourceNodeRid: Buffer;
  sourceSpotRid?: Buffer | null;
  targetNodeRid?: Buffer | null;
  targetSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
  requestHandle: bigint;
}): ActorJoinInfo {
  const sourceActor = actorRefFromRaw(raw.sourceActor ?? raw.actor!);
  const targetActor = actorRefFromRaw(raw.targetActor ?? raw.actor!);
  const info = {
    sourceActor,
    targetActor,
    sourceNodeRid: RoutingId.fromBytes(raw.sourceNodeRid),
    sourceSpotRid: wrapRoutingId(raw.sourceSpotRid ?? null) as RoutingId,
    targetNodeRid: wrapRoutingId(raw.targetNodeRid ?? null) as RoutingId,
    targetSpotRid: wrapRoutingId(raw.targetSpotRid ?? null) as RoutingId,
    joinEpoch: BigInt(raw.joinEpoch ?? 0),
    flags: raw.flags
  };
  actorJoinRequestHandles.set(info, BigInt(raw.requestHandle));
  return info;
}

function actorJoinInfoToRaw(info: ActorJoinInfo): Record<string, unknown> {
  const requestHandle = actorJoinRequestHandles.get(info);
  if (typeof requestHandle !== 'bigint') {
    throw new TypeError('join info must come from recvActorJoin()');
  }
  return {
    actor: actorRefToRaw(info.targetActor),
    sourceActor: actorRefToRaw(info.sourceActor),
    targetActor: actorRefToRaw(info.targetActor),
    sourceNodeRid: normalizeRoutingId(info.sourceNodeRid, 'info.sourceNodeRid'),
    sourceSpotRid: normalizeRoutingId(info.sourceSpotRid, 'info.sourceSpotRid'),
    targetNodeRid: normalizeRoutingId(info.targetNodeRid, 'info.targetNodeRid'),
    targetSpotRid: normalizeRoutingId(info.targetSpotRid, 'info.targetSpotRid'),
    joinEpoch: BigInt(info.joinEpoch ?? 0),
    flags: info.flags | 0,
    requestHandle
  };
}

function actorCreateResultFromRaw(raw: {
  status: number;
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
}): ActorCreateResult {
  return { status: raw.status as ActorCreateStatus, actor: actorRefFromRaw(raw.actor) };
}

function actorRouteFromRaw(raw: {
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  joined: boolean;
  joinedSpotRid: Buffer;
}): ActorRoute {
  return {
    actor: actorRefFromRaw(raw.actor),
    joined: Boolean(raw.joined),
    joinedSpotRid: wrapRoutingId(raw.joinedSpotRid)
  };
}

function spotNodeSpotEntryFromRaw(raw: {
  spotRid: Buffer;
  dispatchHandlerAttached: boolean;
  joinedActorCount: number;
  pendingActorJoinCount: number;
  routeSynced: boolean;
  lastChangedMs: bigint | number;
}): SpotNodeSpotEntry {
  return {
    spotRid: RoutingId.fromBytes(raw.spotRid),
    dispatchHandlerAttached: Boolean(raw.dispatchHandlerAttached),
    joinedActorCount: raw.joinedActorCount,
    pendingActorJoinCount: raw.pendingActorJoinCount,
    routeSynced: Boolean(raw.routeSynced),
    lastChangedMs: BigInt(raw.lastChangedMs)
  };
}

function spotNodeActorEntryFromRaw(raw: {
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  joined: boolean;
  joinedSpotRid: Buffer;
  routeSynced: boolean;
  pendingMessageCount: number;
  lastChangedMs: bigint | number;
}): SpotNodeActorEntry {
  return {
    actor: actorRefFromRaw(raw.actor),
    joined: Boolean(raw.joined),
    joinedSpotRid: wrapRoutingId(raw.joinedSpotRid),
    routeSynced: Boolean(raw.routeSynced),
    pendingMessageCount: raw.pendingMessageCount,
    lastChangedMs: BigInt(raw.lastChangedMs)
  };
}

function normalizeTopologyFilter(filter?: RegistryTopologyFilter): Record<string, unknown> | undefined {
  if (!filter) {
    return undefined;
  }
  return {
    autoConnectType: filter.autoConnectType,
    serviceKind: filter.serviceKind,
    serviceRole: filter.serviceRole,
    channelName: filter.channelName,
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
  return Received.create(
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
}): TopicMessage {
  return TopicMessage.create(
    raw.topic,
    raw.parts.map((part) => Message.fromSnapshot(part)),
    wrapRoutingId(raw.routingId ?? null)
  );
}

class NativeHandle {
  /** @internal */
  protected _native: unknown | null;

  protected constructor(native: unknown) {
    this._native = native;
  }

  /** @internal */
  nativeHandle(): unknown {
    return this._native;
  }

  close(): void {
    this._native = null;
  }
}

class SocketBase extends NativeHandle {
  constructor(ctx: Context, type: number) {
    super(requireNative().socketNew(ctx.nativeHandle(), type));
    if (!this._native) {
      throw lastError('config', 'socket creation failed');
    }
  }

  bind(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    bindCall('socket bind failed', () => {
      requireNative().socketBind(this.nativeHandle(), normalizedEndpoint);
    });
  }

  unbind(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('socket unbind failed', () => {
      requireNative().socketUnbind(this.nativeHandle(), normalizedEndpoint);
    });
  }

  setTlsServer(cert: string, key: string, requireClientCert: boolean = false): void {
    const normalizedCert = validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER);
    const normalizedKey = validateCString(key, 'key', Number.MAX_SAFE_INTEGER);
    configCall('socket TLS server configuration failed', () => {
      requireNative().socketSetTlsServer(
        this.nativeHandle(),
        normalizedCert,
        normalizedKey,
        requireClientCert ? 1 : 0
      );
    });
  }

  setTlsClient(ca: string, hostname: string, trustSystem: boolean = false): void {
    const normalizedCa = validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER);
    const normalizedHostname = validateCString(hostname, 'hostname', Number.MAX_SAFE_INTEGER);
    configCall('socket TLS client configuration failed', () => {
      requireNative().socketSetTlsClient(
        this.nativeHandle(),
        normalizedCa,
        normalizedHostname,
        trustSystem ? 1 : 0
      );
    });
  }

  /** @internal */
  setSockOptRaw(option: number, value: Buffer | number): void {
    const buf = typeof value === 'number' ? Buffer.from([value & 0xff, 0, 0, 0]) : value;
    configCall('socket option set failed', () => {
      requireNative().socketSetOpt(this.nativeHandle(), option | 0, buf);
    });
  }

  /** @internal */
  getSockOptRaw(option: number): Buffer {
    return configCall('socket option get failed', () =>
      requireNative().socketGetOpt(this.nativeHandle(), option | 0) as Buffer
    );
  }

  monitorOpen(events?: readonly MonitorEventType[]): MonitorSocket {
    const mask = events === undefined
      ? 0xFFFF
      : events.reduce((current, event) => current | (event | 0), 0);
    return new MonitorSocket(configCall('monitor open failed', () =>
      requireNative().monitorOpen(this.nativeHandle(), mask | 0)
    ));
  }

  close(): void {
    if (!this._native) return;
    closeCall('socket close failed', () => {
      requireNative().socketClose(this._native);
    });
    this._native = null;
  }
}

class ConnectableSocket extends SocketBase {
  connect(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('socket connect failed', () => {
      requireNative().socketConnect(this.nativeHandle(), normalizedEndpoint);
    });
  }

  disconnect(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('socket disconnect failed', () => {
      requireNative().socketDisconnect(this.nativeHandle(), normalizedEndpoint);
    });
  }

  disconnectRid(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    connectCall('socket disconnect by routing id failed', () => {
      requireNative().socketDisconnectRid(
        this.nativeHandle(),
        normalizedRoutingId
      );
    });
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

function int64Buffer(value: bigint, name: string): Buffer {
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

function boolBuffer(value: boolean): Buffer {
  const buf = Buffer.allocUnsafe(4);
  buf.writeUInt32LE(value ? 1 : 0, 0);
  return buf;
}

function flagsToMask(events: readonly PollEventFlagValue[]): number {
  if (!Array.isArray(events)) {
    throw new TypeError('events must be an array');
  }
  return events.reduce((mask, event) => mask | (event | 0), 0);
}

const POLL_EVENT_FLAGS = Object.freeze([
  PollEventFlag.PollIn,
  PollEventFlag.PollOut,
  PollEventFlag.PollErr,
  PollEventFlag.PollPri
]);

function maskToFlags(mask: number): PollEventFlagValue[] {
  const value = mask | 0;
  const flags: PollEventFlagValue[] = [];
  for (const flag of POLL_EVENT_FLAGS) {
    if ((value & flag) !== 0) flags.push(flag);
  }
  return flags;
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

function readRoutingIdOption(buffer: Buffer): RoutingId | null {
  return buffer.length === 0 ? null : RoutingId.fromBytes(buffer);
}

function readStringOption(buffer: Buffer): string {
  const nul = buffer.indexOf(0);
  return buffer.subarray(0, nul >= 0 ? nul : buffer.length).toString();
}

const OPTION_CREATE_TOKEN = Symbol('OptionFacade.create');

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
  get autoHwmMsgUnitBytes(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.AUTO_HWM_MSG_UNIT_BYTES), 'autoHwmMsgUnitBytes'); }
  set autoHwmMsgUnitBytes(value: number) { this._socket.setSockOptRaw(SocketOption.AUTO_HWM_MSG_UNIT_BYTES, int32Buffer(value, 'autoHwmMsgUnitBytes')); }
  get lastEndpoint(): string { return readStringOption(this._socket.getSockOptRaw(SocketOption.LAST_ENDPOINT)); }
  get backlog(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.BACKLOG), 'backlog'); }
  set backlog(value: number) { this._socket.setSockOptRaw(SocketOption.BACKLOG, int32Buffer(value, 'backlog')); }
  get reconnectInterval(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RECONNECT_IVL), 'reconnectInterval'); }
  set reconnectInterval(value: number) { this._socket.setSockOptRaw(SocketOption.RECONNECT_IVL, int32Buffer(value, 'reconnectInterval')); }
  get reconnectIntervalMax(): number { return readInt32Option(this._socket.getSockOptRaw(SocketOption.RECONNECT_IVL_MAX), 'reconnectIntervalMax'); }
  set reconnectIntervalMax(value: number) { this._socket.setSockOptRaw(SocketOption.RECONNECT_IVL_MAX, int32Buffer(value, 'reconnectIntervalMax')); }
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
  private _verbose = false;
  private _verboser = false;
  private _noDrop = true;
  private _manual = false;
  private _manualLastValue = false;
  private _welcomeMessage = Buffer.alloc(0);

  /** @internal */
  private constructor(token: symbol, socket: SocketBase) { super(token, socket); }
  /** @internal */
  static create(socket: SocketBase): PubSocketOptions {
    return new PubSocketOptions(OPTION_CREATE_TOKEN, socket);
  }
  get verbose(): boolean { return this._verbose; }
  set verbose(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_VERBOSE, boolBuffer(value));
    this._verbose = value;
    this._verboser = false;
  }
  get verboser(): boolean { return this._verboser; }
  set verboser(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_VERBOSER, boolBuffer(value));
    this._verbose = value;
    this._verboser = value;
  }
  get noDrop(): boolean { return this._noDrop; }
  set noDrop(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_NODROP, boolBuffer(value));
    this._noDrop = value;
  }
  get manual(): boolean { return this._manual; }
  set manual(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_MANUAL, boolBuffer(value));
    this._manual = value;
  }
  get manualLastValue(): boolean { return this._manualLastValue; }
  set manualLastValue(value: boolean) {
    this._socket.setSockOptRaw(SocketOption.XPUB_MANUAL_LAST_VALUE, boolBuffer(value));
    this._manual = value;
    this._manualLastValue = value;
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
export class ContextOptions {
  /** @internal */
  protected readonly _context: Context;
  private _threadNamePrefix = '';
  /** @internal */
  private constructor(token: symbol, context: Context) {
    if (token !== OPTION_CREATE_TOKEN) {
      throw new TypeError('context options are created by contexts');
    }
    this._context = context;
  }
  /** @internal */
  static create(context: Context): ContextOptions {
    return new ContextOptions(OPTION_CREATE_TOKEN, context);
  }
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
  get autoHwmEnabled(): boolean { return this._context.getOptionRawInternal(ContextOption.AUTO_HWM_ENABLE) !== 0; }
  set autoHwmEnabled(value: boolean) { this._context.setOptionRawInternal(ContextOption.AUTO_HWM_ENABLE, value ? 1 : 0); }
  get autoHwmRecalcDebounceMs(): number { return this._context.getOptionRawInternal(ContextOption.AUTO_HWM_RECALC_DEBOUNCE_MS); }
  set autoHwmRecalcDebounceMs(value: number) { this._context.setOptionRawInternal(ContextOption.AUTO_HWM_RECALC_DEBOUNCE_MS, value | 0); }
  get autoHwmProfile(): AutoHwmProfileValue { return this._context.getOptionRawInternal(ContextOption.AUTO_HWM_PROFILE) as AutoHwmProfileValue; }
  set autoHwmProfile(value: AutoHwmProfileValue) { this._context.setOptionRawInternal(ContextOption.AUTO_HWM_PROFILE, value | 0); }
  get threadNamePrefix(): string { return this._threadNamePrefix; }
  set threadNamePrefix(value: string) {
    const normalized = validateCString(value, 'threadNamePrefix');
    this._context.setOptionRawInternal(ContextOption.THREAD_NAME_PREFIX, Buffer.from(normalized));
    this._threadNamePrefix = normalized;
  }
  addThreadAffinity(cpu: number): void { this._context.setOptionRawInternal(ContextOption.THREAD_AFFINITY_CPU_ADD, cpu | 0); }
  removeThreadAffinity(cpu: number): void { this._context.setOptionRawInternal(ContextOption.THREAD_AFFINITY_CPU_REMOVE, cpu | 0); }
}

export class Context extends NativeHandle {
  readonly options: ContextOptions;
  constructor() {
    super(requireNative().ctxNew());
    if (!this._native) throw lastError('config', 'context creation failed');
    this.options = ContextOptions.create(this);
  }
  /** @internal */
  nativeHandle(): unknown { return this._native; }
  /** @internal */
  setOptionRawInternal(option: number, value: Buffer | number): void {
    configCall('context option set failed', () => {
      requireNative().ctxSetOpt(this._native, option | 0, typeof value === 'number' ? value | 0 : value);
    });
  }
  /** @internal */
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
      throw createError('config', readErrno(), nativeErrorMessage(error, 'context option get failed'));
    }
  }
  /** @internal */
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
    closeCall('context shutdown failed', () => {
      requireNative().ctxShutdown(this._native);
    });
  }
  recalculateAutoHwm(): void {
    configCall('context auto HWM recalculation failed', () => {
      requireNative().ctxRecalculateAutoHwm(this._native);
    });
  }
  close(): void {
    if (!this._native) return;
    closeCall('context close failed', () => {
      requireNative().ctxTerm(this._native);
    });
    this._native = null;
  }
}

export class MonitorSocket extends NativeHandle {
  static readonly ignoreHandler: SocketMonitorHandler = () => {};
  recv(flags: RecvFlags = RecvFlags.None): MonitorEvent | null {
    try {
      if ((flags | 0) & (RecvFlags.DontWait | 0)) {
        const raw = requireNative().monitorRecvNoWait(this._native) as MonitorEventValueRaw | null;
        return raw ? MonitorEvent.create(raw) : null;
      }
      return MonitorEvent.create(requireNative().monitorRecv(this._native) as MonitorEventValueRaw);
    } catch (error) {
      throw recvNativeError(error, flags, 'monitor recv failed');
    }
  }
  onEvent(handler: SocketMonitorHandler): void {
    handlerCall('monitor handler registration failed', () => {
      requireNative().monitorHandler(this._native, (event: MonitorEventValueRaw) => {
        handler(MonitorEvent.create(event));
      });
    });
  }
  snapshot(): MonitorSnapshot {
    return materializeMonitorSnapshot(configCall('monitor snapshot failed', () =>
      requireNative().monitorSnapshot(this._native) as MonitorSnapshotRaw
    ));
  }
  close(): void {
    if (this._native) {
      closeCall('monitor close failed', () => {
        requireNative().monitorClose(this._native);
      });
      this._native = null;
    }
  }
}

class SendSocket extends ConnectableSocket {
  send(message: MessageLike, flags?: SendFlags): boolean;
  send(parts: readonly MessageLike[], flags?: SendFlags): boolean;
  send(payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const payload = normalizeMessageLikePayload(payloadOrParts);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = Array.isArray(payload)
          ? requireNative().socketSendNoWaitResultParts(this.nativeHandle(), payload) as number
          : requireNative().socketSendNoWaitResult(this.nativeHandle(), payload) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'send failed');
      }
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
  publish(topic: string, message: MessageLike, flags?: SendFlags): boolean;
  publish(topic: string, parts: readonly MessageLike[], flags?: SendFlags): boolean;
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
  /**
   * Canonical caller-provided storage recv. Pass a long-lived {@link Received}
   * and the binding refills its internal state in place each successful call.
   * Returns true on success, false when DontWait finds no data. See
   * doc/spec/bindings/README.md "Canonical Recv: Caller-Provided Storage".
   */
  recv(result: Received, flags?: RecvFlags): boolean;
  /** @deprecated use {@link recv}(result, flags) with caller-provided storage. */
  recv(flags?: RecvFlags): Received | null;
  recv(arg0?: Received | RecvFlags, arg1?: RecvFlags): boolean | Received | null {
    if (arg0 instanceof Received) {
      const flags = (arg1 ?? RecvFlags.None);
      const fresh = this._recvLegacy(flags);
      if (fresh == null) return false;
      (arg0 as Received & { _adoptFrom: (s: Received) => void })._adoptFrom(fresh);
      return true;
    }
    return this._recvLegacy((arg0 as RecvFlags | undefined) ?? RecvFlags.None);
  }
  private _recvLegacy(flags: RecvFlags): Received | null {
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
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
}

class SubscriberSocket extends ConnectableSocket {
  setSubscription(topicOrPattern: string): void {
    const topic = Buffer.from(validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER));
    configCall('subscription set failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.SUBSCRIBE | 0,
        topic
      );
    });
  }
  unsetSubscription(topicOrPattern: string): void {
    const topic = Buffer.from(validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER));
    configCall('subscription unset failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.UNSUBSCRIBE | 0,
        topic
      );
    });
  }
  subscriptionAt(index: number): SubscriptionEntry | null {
    return configCall('subscription lookup failed', () =>
      requireNative().subscriptionAt(this.nativeHandle(), index >>> 0) as SubscriptionEntry | null
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
  send(routingId: RoutingId, message: MessageLike, flags?: SendFlags): boolean;
  send(routingId: RoutingId, parts: readonly MessageLike[], flags?: SendFlags): boolean;
  send(routingId: RoutingId, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalized = normalizeMessageLikePayload(payload);
    const normalizedRoutingId = normalizeRoutingId(routingId);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = Array.isArray(normalized)
          ? requireNative().socketSendRoutingNoWaitResultParts(this.nativeHandle(), normalizedRoutingId, normalized) as number
          : requireNative().socketSendRoutingNoWaitResult(this.nativeHandle(), normalizedRoutingId, normalized) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'send failed');
      }
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    const parts = Array.isArray(normalized)
      ? [normalizedRoutingId, ...normalized]
      : [normalizedRoutingId, normalized];
    try {
      requireNative().socketSendParts(this.nativeHandle(), parts, flags | 0);
    } catch (error) {
      throw submitNativeError(error, flags, 'send failed');
    }
    return true;
  }
  /**
   * Canonical caller-provided storage recv. See {@link MessageSocket.recv}.
   */
  recv(result: Received, flags?: RecvFlags): boolean;
  /** @deprecated use {@link recv}(result, flags) with caller-provided storage. */
  recv(flags?: RecvFlags): Received | null;
  recv(arg0?: Received | RecvFlags, arg1?: RecvFlags): boolean | Received | null {
    if (arg0 instanceof Received) {
      const flags = (arg1 ?? RecvFlags.None);
      const fresh = this._recvLegacy(flags);
      if (fresh == null) return false;
      (arg0 as Received & { _adoptFrom: (s: Received) => void })._adoptFrom(fresh);
      return true;
    }
    return this._recvLegacy((arg0 as RecvFlags | undefined) ?? RecvFlags.None);
  }
  private _recvLegacy(flags: RecvFlags): Received | null {
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
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
}

export class PairSocket extends MessageSocket {
  readonly options: CommonSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.PAIR); this.options = CommonSocketOptions.create(this); }
}

export class PubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.PUB); this.options = PubSocketOptions.create(this); }
  onSendReady(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
  attachDiscovery(discovery: Discovery): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
}

export class XPubSocket extends PublisherSocket {
  readonly options: PubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.XPUB); this.options = PubSocketOptions.create(this); }
  receiveSubscriptionEvent(flags: RecvFlags = RecvFlags.None): SubscriptionEvent | null {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTrySubscriptionEvent(this.nativeHandle()) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null
        : requireNative().socketSubscriptionEvent(this.nativeHandle(), flags | 0) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscription event recv failed');
    }
    return raw ? SubscriptionEvent.create(raw.topic, raw.subscribed, wrapRoutingId(raw.routingId ?? null)) : null;
  }
  onSendReady(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
}

export class SubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.SUB); this.options = SubSocketOptions.create(this); }
  attachDiscovery(discovery: Discovery): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
}

export class XSubSocket extends SubscriberSocket {
  readonly options: SubSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.XSUB); this.options = SubSocketOptions.create(this); }
}

export class DealerSocket extends MessageSocket {
  readonly options: DealerSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.DEALER); this.options = DealerSocketOptions.create(this); }
  setChannelName(channelName: string): void {
    const normalized = validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER);
    configCall('channel name set failed', () => {
      requireNative().socketSetChannelName(
        this.nativeHandle(),
        normalized
      );
    });
  }
  getChannelName(): string {
    return configCall('channel name get failed', () =>
      requireNative().socketGetChannelName(this.nativeHandle()) as string
    );
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('routing id set failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.ROUTING_ID | 0,
        normalizedRoutingId
      );
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.fromBytes(
      configCall('routing id get failed', () =>
        requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer
      )
    );
  }
  attachDiscovery(discovery: Discovery): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
  request(message: MessageLike, timeout?: number): Promise<Message[]>;
  request(parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;
  request(message: MessageLike, callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
  request(parts: readonly MessageLike[], callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
  request(
    payloadOrParts: MessageLike | readonly MessageLike[],
    callbackOrTimeout?: RequestCallback | number,
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
              messagesFromNativeBuffers(replyParts)
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
    const timeoutMs = typeof callbackOrTimeout === 'number' ? callbackOrTimeout : 0;
    return new Promise<Message[]>((resolve, reject) => {
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
            if (result !== RequestResult.Ok) {
              reject(requestErrorFromResult(result as RequestResult, 'request failed'));
              return;
            }
            resolve(messagesFromNativeBuffers(replyParts));
          },
          SendFlags.None,
          timeoutMs | 0
        );
      } catch (error) {
        releaseProgress();
        reject(submitNativeError(error, SendFlags.None, 'request failed'));
      }
    });
  }
}

export class RouterSocket extends RoutedMessageSocket {
  readonly options: RouterSocketOptions;
  constructor(ctx: Context) { super(ctx, NativeSocketType.ROUTER); this.options = RouterSocketOptions.create(this); }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('routing id set failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.ROUTING_ID | 0,
        normalizedRoutingId
      );
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.fromBytes(
      configCall('routing id get failed', () =>
        requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer
      )
    );
  }
  attachDiscovery(discovery: Discovery): void {
    configCall('socket discovery attachment failed', () => {
      requireNative().socketAttachDiscovery(this.nativeHandle(), discovery.nativeHandle());
    });
  }
  request(peerRid: RoutingId, message: MessageLike, timeout?: number): Promise<Message[]>;
  request(peerRid: RoutingId, parts: readonly MessageLike[], timeout?: number): Promise<Message[]>;
  request(peerRid: RoutingId, message: MessageLike, callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
  request(peerRid: RoutingId, parts: readonly MessageLike[], callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
  request(
    peerRid: RoutingId,
    payloadOrParts: MessageLike | readonly MessageLike[],
    callbackOrTimeout?: RequestCallback | number,
    flagsOrTimeout?: SendFlags | number,
    maybeTimeout?: number,
  ): Promise<Message[]> | boolean {
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    const peer = normalizeRoutingId(peerRid, 'peerRid');
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
            callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
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
    const timeoutMs = typeof callbackOrTimeout === 'number' ? callbackOrTimeout : 0;
    return new Promise<Message[]>((resolve, reject) => {
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
            if (result !== RequestResult.Ok) {
              reject(requestErrorFromResult(result as RequestResult, 'request failed'));
              return;
            }
            resolve(messagesFromNativeBuffers(replyParts));
          },
          SendFlags.None,
          timeoutMs | 0
        );
      } catch (error) {
        releaseProgress();
        reject(submitNativeError(error, SendFlags.None, 'request failed'));
      }
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
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, message: MessageLike, callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, parts: readonly MessageLike[], callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: MessageLike | readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
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
            callbackOrTimeout(result as RequestResult, messagesFromNativeBuffers(replyParts));
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
      try {
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
            resolve(messagesFromNativeBuffers(replyParts));
          },
          0,
          timeoutMs | 0
        );
      } catch (error) {
        releaseProgress();
        reject(submitNativeError(error, SendFlags.None, 'requestToSpot failed'));
      }
    });
  }
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    const normalizedDestNodeRid = normalizeRoutingId(destNodeRid);
    const normalizedDestSpotRid = normalizeRoutingId(destSpotRid);
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    try {
      requireNative().routerSpotReply(
        this.nativeHandle(),
        normalizedDestNodeRid,
        normalizedDestSpotRid,
        requestSeq,
        parts
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'replyToSpot failed');
    }
  }
  reply(peerRid: RoutingId, requestSeq: bigint, message: MessageLike, flags?: SendFlags): void;
  reply(peerRid: RoutingId, requestSeq: bigint, parts: readonly MessageLike[], flags?: SendFlags): void;
  reply(peerRid: RoutingId, requestSeq: bigint, payloadOrParts: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
    normalizeReplyFlags(flags);
    const normalizedPeerRid = normalizeRoutingId(peerRid, 'peerRid');
    const parts = Array.isArray(payloadOrParts) ? toMessageParts(payloadOrParts) : [normalizeMessageLikePayload(payloadOrParts)];
    try {
      requireNative().routerReply(
        this.nativeHandle(),
        normalizedPeerRid,
        requestSeq,
        parts
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'reply failed');
    }
  }
}

export class Actor extends NativeHandle {
  private static readonly CREATE_TOKEN = Symbol('Actor.create');
  private _ref: ActorRef | null;
  private constructor(token: symbol, nodeHandle: unknown, ref: ActorRef) {
    if (token !== Actor.CREATE_TOKEN) {
      throw new TypeError('Actor values are created by SpotNode.createActor()');
    }
    super(nodeHandle);
    this._ref = ref;
  }
  /** @internal */
  static create(nodeHandle: unknown, ref: ActorRef): Actor {
    return new Actor(Actor.CREATE_TOKEN, nodeHandle, ref);
  }
  /** @internal */
  nativeHandle(): unknown { return this._native; }
  ref(): ActorRef {
    if (!this._ref) {
      throw new Error('actor is closed');
    }
    return this._ref;
  }
  get actorRef(): ActorRef {
    return this.ref();
  }
  join(spot: Spot, message: MessageLike, timeout?: number): Promise<Message[]>;
  join(spot: Spot, message: MessageLike, callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
  join(spot: Spot, message: MessageLike, callbackOrTimeout?: RequestCallback | number, flags?: SendFlags, timeout?: number): Promise<Message[]> | boolean {
    if (!(spot instanceof Spot)) {
      throw new TypeError('spot must be a Spot');
    }
    const submit = (callback: RequestCallback, normalizedFlags: SendFlags, timeoutMs: number): boolean => {
      const releaseProgress = startRequestProgress(spot.nativeHandle(), (handle) => requireNative().spotRequestProgress(handle));
      try {
        requireNative().spotNodeActorJoinSpot(
          this._native,
          actorRefToRaw(this.ref()),
          normalizeRoutingId(spot.ownerNodeRoutingId(), 'destNodeRid'),
          normalizeRoutingId(spot.routingId, 'destSpotRid'),
          [normalizeMessageLikePayload(message)],
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
          },
          normalizedFlags | 0,
          timeoutMs | 0
        );
        return true;
      } catch (error) {
        releaseProgress();
        const submitError = submitNativeError(error, normalizedFlags, 'actor join failed');
        if (((normalizedFlags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
          return false;
        }
        throw submitError;
      }
    };
    if (typeof callbackOrTimeout === 'function') {
      const { flags: normalizedFlags, timeoutMs } = normalizeCallbackFlagsAndTimeout(flags, timeout);
      return submit(callbackOrTimeout, normalizedFlags, timeoutMs);
    }
    const timeoutMs = typeof callbackOrTimeout === 'number' ? callbackOrTimeout : 0;
    return new Promise<Message[]>((resolve, reject) => {
      submit((result, parts) => {
        if (result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result as RequestResult, 'actor join failed'));
          return;
        }
        resolve(parts.slice());
      }, SendFlags.None, timeoutMs);
    });
  }
  leave(spot: Spot, timeoutMs = 0): void {
    if (!(spot instanceof Spot)) {
      throw new TypeError('spot must be a Spot');
    }
    const actorRaw = actorRefToRaw(this.ref());
    const spotRid = normalizeRoutingId(spot.routingId, 'destSpotRid');
    configCall('actor leave failed', () => {
      requireNative().spotNodeActorLeaveSpot(
        this._native,
        actorRaw,
        spotRid,
        timeoutMs | 0
      );
    });
  }
  recvPart(flags: RecvFlags = RecvFlags.None): ActorPart | null {
    let raw;
    try {
      raw = requireNative().spotNodeActorRecvPart(
        this._native,
        actorRefToRaw(this.ref()),
        flags | 0
      ) as any | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'actor part recv failed');
    }
    return raw ? actorPartFromRaw(raw) : null;
  }
  sendBoundSession(message: MessageLike, flags: SendFlags = SendFlags.None): boolean {
    try {
      requireNative().spotNodeActorSendBoundSessionMsg(
        this._native,
        actorRefToRaw(this.ref()),
        [normalizeMessageLikePayload(message)],
        flags | 0
      );
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'actor bound session send failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
    return true;
  }
  closeBoundSession(timeoutMs = 0): void {
    const actorRaw = actorRefToRaw(this.ref());
    configCall('actor bound session close failed', () => {
      requireNative().spotNodeActorCloseBoundSession(
        this._native,
        actorRaw,
        timeoutMs | 0
      );
    });
  }
  close(timeoutMs = 0): void {
    if (!this._native || !this._ref) {
      return;
    }
    const actorRaw = actorRefToRaw(this._ref);
    closeCall('actor close failed', () => {
      requireNative().spotNodeActorDestroy(this._native, actorRaw, timeoutMs | 0);
    });
    this._ref = null;
    this._native = null;
  }
}

export class StreamSocket extends SocketBase {
  readonly options: StreamSocketOptions;
  constructor(ctx: Context) {
    super(ctx, NativeSocketType.STREAM);
    this.options = StreamSocketOptions.create(this);
  }
  send(routingId: RoutingId, message: MessageLike, flags?: SendFlags): boolean;
  send(routingId: RoutingId, parts: readonly MessageLike[], flags?: SendFlags): boolean;
  send(routingId: RoutingId, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalized = normalizeMessageLikePayload(payload);
    const normalizedRoutingId = normalizeRoutingId(routingId);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = Array.isArray(normalized)
          ? requireNative().socketSendRoutingNoWaitResultParts(this.nativeHandle(), normalizedRoutingId, normalized) as number
          : requireNative().socketSendRoutingNoWaitResult(this.nativeHandle(), normalizedRoutingId, normalized) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'send failed');
      }
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    const parts = Array.isArray(normalized)
      ? [normalizedRoutingId, ...normalized]
      : [normalizedRoutingId, normalized];
    try {
      requireNative().socketSendParts(this.nativeHandle(), parts, flags | 0);
    } catch (error) {
      throw submitNativeError(error, flags, 'send failed');
    }
    return true;
  }
  /**
   * Canonical caller-provided storage recv. See {@link MessageSocket.recv}.
   */
  recv(result: Received, flags?: RecvFlags): boolean;
  /** @deprecated use {@link recv}(result, flags) with caller-provided storage. */
  recv(flags?: RecvFlags): Received | null;
  recv(arg0?: Received | RecvFlags, arg1?: RecvFlags): boolean | Received | null {
    if (arg0 instanceof Received) {
      const flags = (arg1 ?? RecvFlags.None);
      const fresh = this._recvLegacy(flags);
      if (fresh == null) return false;
      (arg0 as Received & { _adoptFrom: (s: Received) => void })._adoptFrom(fresh);
      return true;
    }
    return this._recvLegacy((arg0 as RecvFlags | undefined) ?? RecvFlags.None);
  }
  private _recvLegacy(flags: RecvFlags): Received | null {
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
    handlerCall('stream packet handler registration failed', () => {
      requireNative().socketStreamAttach(
        this.nativeHandle(),
        (routingId: Buffer | null, packets: Buffer[]) => {
          const sourceRid = wrapRoutingId(routingId);
          if (!sourceRid) {
            return 0;
          }
          const header = messageFromNativeBuffer(packets[0]);
          const body = messageFromNativeBuffer(packets[1]);
          handler(sourceRid, header, body);
          return 0;
        },
        1
      );
    });
  }
  onSendReady(handler: SocketSendReadyHandler): void {
    handlerCall('send-ready handler registration failed', () => {
      requireNative().socketSendReadyHandler(this.nativeHandle(), handler);
    });
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('routing id set failed', () => {
      requireNative().socketSetOpt(
        this.nativeHandle(),
        SocketOption.ROUTING_ID | 0,
        normalizedRoutingId
      );
    });
  }
  getRoutingId(): RoutingId {
    return RoutingId.fromBytes(
      configCall('routing id get failed', () =>
        requireNative().socketGetOpt(this.nativeHandle(), SocketOption.ROUTING_ID | 0) as Buffer
      )
    );
  }
  bindActor(node: SpotNode, sessionRid: RoutingId, actor: ActorRef, timeoutMs = 0): void {
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const actorRaw = actorRefToRaw(actor);
    configCall('stream actor bind failed', () => {
      requireNative().streamBindActor(
        node.nativeHandle(),
        this.nativeHandle(),
        normalizedSessionRid,
        actorRaw,
        timeoutMs | 0
      );
    });
  }
  unbindActor(node: SpotNode, sessionRid: RoutingId, actorId: string, timeoutMs = 0): void {
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    configCall('stream actor unbind failed', () => {
      requireNative().streamUnbindActor(
        node.nativeHandle(),
        this.nativeHandle(),
        normalizedSessionRid,
        normalizedActorId,
        timeoutMs | 0
      );
    });
  }
  sendBoundActor(node: SpotNode, sessionRid: RoutingId, actorId: string, message: MessageLike, flags?: SendFlags): boolean;
  sendBoundActor(node: SpotNode, sessionRid: RoutingId, actorId: string, parts: readonly MessageLike[], flags?: SendFlags): boolean;
  sendBoundActor(node: SpotNode, sessionRid: RoutingId, actorId: string, payload: MessageLike | readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const parts = Array.isArray(payload) ? toMessageParts(payload) : [normalizeMessageLikePayload(payload)];
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    try {
      requireNative().streamSendBoundActorPart(
        node.nativeHandle(),
        this.nativeHandle(),
        normalizedSessionRid,
        normalizedActorId,
        parts,
        flags | 0
      );
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'sendBoundActor failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
    return true;
  }
}

export type BaseSocket =
  PairSocket | PubSocket | SubSocket | DealerSocket | RouterSocket |
  XPubSocket | XSubSocket | StreamSocket;

export class Registry extends NativeHandle {
  private _bound = false;
  constructor(ctx: Context) { super(requireNative().registryNew(ctx.nativeHandle())); }
  /** @internal */
  nativeHandle(): unknown { return this._native; }
  bind(pubEndpoint: string, routerEndpoint: string): void {
    const normalizedPubEndpoint = validateCString(pubEndpoint, 'pubEndpoint');
    const normalizedRouterEndpoint = validateCString(routerEndpoint, 'routerEndpoint');
    bindCall('registry bind failed', () => {
      requireNative().registrySetEndpoints(this._native, normalizedPubEndpoint, normalizedRouterEndpoint);
    });
    this._bound = true;
  }
  setId(id: number): void {
    configCall('registry id set failed', () => {
      requireNative().registrySetId(this._native, id | 0);
    });
  }
  addPeer(pubEndpoint: string): void {
    const normalizedPubEndpoint = validateCString(pubEndpoint, 'pubEndpoint');
    configCall('registry peer add failed', () => {
      requireNative().registryAddPeer(this._native, normalizedPubEndpoint);
    });
  }
  setHeartbeat(intervalMs: number, timeoutMs: number): void {
    configCall('registry heartbeat configuration failed', () => {
      requireNative().registrySetHeartbeat(this._native, intervalMs | 0, timeoutMs | 0);
    });
  }
  setBroadcastInterval(intervalMs: number): void {
    configCall('registry broadcast interval configuration failed', () => {
      requireNative().registrySetBroadcastInterval(this._native, intervalMs | 0);
    });
  }
  setTlsServer(cert: string, key: string, requireClientCert: boolean = false): void {
    const normalizedCert = validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER);
    const normalizedKey = validateCString(key, 'key', Number.MAX_SAFE_INTEGER);
    configCall('registry TLS server configuration failed', () => {
      requireNative().registrySetTlsServer(this._native, normalizedCert, normalizedKey, requireClientCert ? 1 : 0);
    });
  }
  setTlsClient(ca: string, hostname: string, trustSystem: boolean = false): void {
    const normalizedCa = validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER);
    const normalizedHostname = validateCString(hostname, 'hostname', Number.MAX_SAFE_INTEGER);
    configCall('registry TLS client configuration failed', () => {
      requireNative().registrySetTlsClient(this._native, normalizedCa, normalizedHostname, trustSystem ? 1 : 0);
    });
  }
  statusSnapshot(): RegistryStatus {
    return mapRegistryStatus(configCall('registry status snapshot failed', () =>
      requireNative().registryStatusSnapshot(this._native)
    ));
  }
  serviceSummarySnapshot(filter?: RegistryServiceSummaryFilter): RegistryServiceSummaryEntry[] {
    return (configCall('registry service summary snapshot failed', () =>
      requireNative().registryServiceSummarySnapshot(this._native, filter ?? undefined) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapRegistryServiceSummaryEntry(entry as any));
  }
  topologySnapshot(): RegistryTopologyEntry[] {
    return (configCall('registry topology snapshot failed', () =>
      requireNative().registryTopologySnapshot(this._native) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapRegistryTopologyEntry(entry as any));
  }
  topologyQuery(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    const normalizedFilter = normalizeTopologyFilter(filter);
    return (configCall('registry topology query failed', () =>
      requireNative().registryTopologyQuery(this._native, normalizedFilter) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapRegistryTopologyEntry(entry as any));
  }
  memberPeers(channelName: string): MemberPeerEntry[] {
    const normalizedChannelName = validateCString(channelName, 'channelName');
    return (configCall('registry member peer query failed', () =>
      requireNative().registryMemberPeers(this._native, normalizedChannelName) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapMemberPeerEntry(entry as any));
  }
  close(): void {
    if (this._native) {
      closeCall('registry close failed', () => {
        requireNative().registryDestroy(this._native);
      });
      this._native = null;
    }
  }
}

export class RegistryQueryClient extends NativeHandle {
  constructor(ctx: Context) { super(requireNative().registryQueryClientNew(ctx.nativeHandle())); }
  connect(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('registry query client connect failed', () => {
      requireNative().registryQueryClientConnect(this._native, normalizedEndpoint);
    });
  }
  snapshot(filter?: RegistryTopologyFilter): RegistryTopologyEntry[] {
    const normalizedFilter = normalizeTopologyFilter(filter);
    return (configCall('registry query snapshot failed', () =>
      requireNative().registryQuerySnapshot(this._native, normalizedFilter) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapRegistryTopologyEntry(entry as any));
  }
  close(): void {
    if (this._native) {
      closeCall('registry query client close failed', () => {
        requireNative().registryQueryDestroy(this._native);
      });
      this._native = null;
    }
  }
}

export class Discovery extends NativeHandle {
  readonly autoConnectType: AutoConnectType;
  readonly channelName: string;
  constructor(ctx: Context, autoConnectType: AutoConnectType, channelName: string) {
    if (typeof channelName !== 'string' || channelName.length === 0) throw new TypeError('Discovery channelName must be a non-empty string');
    validateCString(channelName, 'channelName');
    super(requireNative().discoveryNew(ctx.nativeHandle(), autoConnectType, channelName));
    this.autoConnectType = autoConnectType;
    this.channelName = channelName;
  }
  /** @internal */
  nativeHandle(): unknown { return this._native; }
  connectRegistry(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('discovery registry connect failed', () => {
      requireNative().discoveryConnectRegistry(this._native, normalizedEndpoint);
    });
  }
  setValue(value: number): void {
    configCall('discovery value set failed', () => {
      requireNative().discoverySetValue(this._native, value | 0);
    });
  }
  getValue(): number {
    return configCall('discovery value get failed', () =>
      requireNative().discoveryGetValue(this._native) as number
    );
  }
  resolveSpot(spotRid: RoutingId): RoutingId {
    const normalizedSpotRid = normalizeRoutingId(spotRid);
    return RoutingId.fromBytes(
      configCall('discovery spot resolve failed', () =>
        requireNative().discoveryResolveSpot(this._native, normalizedSpotRid) as Buffer
      )
    );
  }
  resolveActor(actorId: string): ActorRoute {
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return actorRouteFromRaw(
      configCall('discovery actor resolve failed', () =>
        requireNative().discoveryResolveActor(
          this._native,
          normalizedActorId
        ) as any
      )
    );
  }
  get spotOwnerSyncEnabled(): boolean {
    return readInt32Option(
      configCall('discovery spot-owner sync option get failed', () =>
        requireNative().socketGetOpt(this._native, SocketOption.DISCOVERY_SPOT_OWNER_SYNC) as Buffer
      ),
      'spotOwnerSyncEnabled'
    ) !== 0;
  }
  set spotOwnerSyncEnabled(enabled: boolean) {
    const value = boolBuffer(enabled);
    configCall('discovery spot-owner sync option set failed', () => {
      requireNative().socketSetOpt(
        this._native,
        SocketOption.DISCOVERY_SPOT_OWNER_SYNC,
        value
      );
    });
  }
  get actorRouteSyncEnabled(): boolean {
    return readInt32Option(
      configCall('discovery actor-route sync option get failed', () =>
        requireNative().socketGetOpt(this._native, SocketOption.DISCOVERY_ACTOR_ROUTE_SYNC) as Buffer
      ),
      'actorRouteSyncEnabled'
    ) !== 0;
  }
  set actorRouteSyncEnabled(enabled: boolean) {
    const value = boolBuffer(enabled);
    configCall('discovery actor-route sync option set failed', () => {
      requireNative().socketSetOpt(
        this._native,
        SocketOption.DISCOVERY_ACTOR_ROUTE_SYNC,
        value
      );
    });
  }
  memberPeers(): MemberPeerEntry[] {
    return (configCall('discovery member peer query failed', () =>
      requireNative().discoveryGetProviders(this._native) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapMemberPeerEntry(entry as any));
  }
  setTlsClient(ca: string, hostname: string, trustSystem: boolean = false): void {
    const normalizedCa = validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER);
    const normalizedHostname = validateCString(hostname, 'hostname', Number.MAX_SAFE_INTEGER);
    configCall('discovery TLS client configuration failed', () => {
      requireNative().discoverySetTlsClient(this._native, normalizedCa, normalizedHostname, trustSystem ? 1 : 0);
    });
  }
  close(): void {
    if (this._native) {
      closeCall('discovery close failed', () => {
        requireNative().discoveryDestroy(this._native);
      });
      this._native = null;
    }
  }
}

export class SpotNode extends NativeHandle {
  private readonly _spots = new Set<Spot>();
  private readonly _channelDealers = new Map<string, DealerSocket>();
  private _nodeRoutingId: RoutingId;
  constructor(ctx: Context, mode: SpotNodeModeValue = SpotNodeMode.All) {
    super(requireNative().spotNodeNew(ctx.nativeHandle(), { mode: mode | 0 }));
    this._nodeRoutingId = RoutingId.fromBytes(randomBytes(16));
  }
  /** @internal */
  nativeHandle(): unknown { return this._native; }
  bind(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    bindCall('spot node bind failed', () => {
      requireNative().spotNodeBind(this._native, normalizedEndpoint);
    });
  }
  connectPeer(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('spot node peer connect failed', () => {
      requireNative().spotNodeConnectPeerPub(this._native, normalizedEndpoint);
    });
  }
  disconnectPeer(endpoint: string): void {
    const normalizedEndpoint = validateCString(endpoint, 'endpoint');
    connectCall('spot node peer disconnect failed', () => {
      requireNative().spotNodeDisconnectPeerPub(this._native, normalizedEndpoint);
    });
  }
  disconnectPeerRid(targetNodeRid: RoutingId): void {
    const normalizedTargetNodeRid = normalizeRoutingId(targetNodeRid);
    connectCall('spot node peer disconnect by routing id failed', () => {
      requireNative().spotNodeDisconnectPeerRidPub(
        this._native,
        normalizedTargetNodeRid
      );
    });
  }
  attachChannelDealer(discovery: Discovery, dealer: DealerSocket): void {
    configCall('spot node channel dealer attachment failed', () => {
      requireNative().spotNodeAttachChannelDealer(
        this._native,
        discovery.nativeHandle(),
        dealer.nativeHandle()
      );
    });
  }
  attachChannelDealerManual(channelName: string, dealer: DealerSocket): void {
    const normalized = validateCString(channelName, 'channelName');
    configCall('spot node channel dealer attachment failed', () => {
      requireNative().spotNodeAttachChannelDealerManual(
        this._native,
        normalized,
        dealer.nativeHandle()
      );
    });
    this._channelDealers.set(normalized, dealer);
  }
  attachPubIngress(pub: PubSocket): void {
    configCall('spot node pub ingress attachment failed', () => {
      requireNative().spotNodeAttachPubIngress(this._native, pub.nativeHandle());
    });
  }
  attachDiscovery(discovery: Discovery): void {
    configCall('spot node discovery attachment failed', () => {
      requireNative().spotNodeSetDiscovery(this._native, discovery.nativeHandle());
    });
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('spot node routing id set failed', () => {
      requireNative().handleSetRoutingId(this._native, normalizedRoutingId);
    });
    this._nodeRoutingId = routingId;
  }
  get routingId(): RoutingId {
    this._nodeRoutingId = RoutingId.fromBytes(configCall('spot node routing id get failed', () =>
      requireNative().handleGetRoutingId(this._native) as Buffer
    ));
    return this._nodeRoutingId;
  }
  setTlsServer(cert: string, key: string, requireClientCert: boolean = false): void {
    const normalizedCert = validateCString(cert, 'cert', Number.MAX_SAFE_INTEGER);
    const normalizedKey = validateCString(key, 'key', Number.MAX_SAFE_INTEGER);
    configCall('spot node TLS server configuration failed', () => {
      requireNative().spotNodeSetTlsServer(this._native, normalizedCert, normalizedKey, requireClientCert ? 1 : 0);
    });
  }
  setTlsClient(ca: string, hostname: string, trustSystem: boolean = false): void {
    const normalizedCa = validateCString(ca, 'ca', Number.MAX_SAFE_INTEGER);
    const normalizedHostname = validateCString(hostname, 'hostname', Number.MAX_SAFE_INTEGER);
    configCall('spot node TLS client configuration failed', () => {
      requireNative().spotNodeSetTlsClient(this._native, normalizedCa, normalizedHostname, trustSystem ? 1 : 0);
    });
  }
  private setOptionRaw(option: number, value: number): void {
    const buffer = int32Buffer(value, 'value');
    configCall('spot node option set failed', () => {
      requireNative().spotNodeSetOption(this._native, option, buffer);
    });
  }
  private getOptionRaw(option: number, name: string): number {
    return readInt32Option(configCall('spot node option get failed', () =>
      requireNative().spotNodeGetOption(this._native, option) as Buffer
    ), name);
  }
  get routerHwmProfile(): AutoHwmProfileValue { return this.getOptionRaw(SpotNodeOption.ROUTER_HWM_PROFILE, 'routerHwmProfile') as AutoHwmProfileValue; }
  set routerHwmProfile(value: AutoHwmProfileValue) { this.setOptionRaw(SpotNodeOption.ROUTER_HWM_PROFILE, value | 0); }
  get routerHwm(): number { return this.getOptionRaw(SpotNodeOption.ROUTER_HWM, 'routerHwm'); }
  set routerHwm(value: number) { this.setOptionRaw(SpotNodeOption.ROUTER_HWM, value | 0); }
  get pubsubHwmProfile(): AutoHwmProfileValue { return this.getOptionRaw(SpotNodeOption.PUBSUB_HWM_PROFILE, 'pubsubHwmProfile') as AutoHwmProfileValue; }
  set pubsubHwmProfile(value: AutoHwmProfileValue) { this.setOptionRaw(SpotNodeOption.PUBSUB_HWM_PROFILE, value | 0); }
  get pubsubHwm(): number { return this.getOptionRaw(SpotNodeOption.PUBSUB_HWM, 'pubsubHwm'); }
  set pubsubHwm(value: number) { this.setOptionRaw(SpotNodeOption.PUBSUB_HWM, value | 0); }
  get dispatchWorkersMin(): number { return this.getOptionRaw(SpotNodeOption.DISPATCH_WORKERS_MIN, 'dispatchWorkersMin'); }
  set dispatchWorkersMin(value: number) { this.setOptionRaw(SpotNodeOption.DISPATCH_WORKERS_MIN, value | 0); }
  get dispatchWorkersMax(): number { return this.getOptionRaw(SpotNodeOption.DISPATCH_WORKERS_MAX, 'dispatchWorkersMax'); }
  set dispatchWorkersMax(value: number) { this.setOptionRaw(SpotNodeOption.DISPATCH_WORKERS_MAX, value | 0); }
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
  entrySpot(): Spot {
    const spot = Spot.fromNative(this, configCall('spot node entry spot lookup failed', () =>
      requireNative().spotNodeEntrySpot(this._native)
    ));
    this._spots.add(spot);
    return spot;
  }
  spotLookup(spotRid: RoutingId): Spot | null {
    const normalizedSpotRid = normalizeRoutingId(spotRid, 'spotRid');
    const native = configCall('spot node spot lookup failed', () =>
      requireNative().spotNodeSpotLookup(this._native, normalizedSpotRid)
    );
    if (!native) return null;
    const spot = Spot.fromNative(this, native);
    this._spots.add(spot);
    return spot;
  }
  createActor(actorId: string): Actor {
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return Actor.create(
      this._native,
      actorRefFromRaw(configCall('spot node actor create failed', () =>
        requireNative().spotNodeActorNew(
          this._native,
          normalizedActorId
        ) as any
      ))
    );
  }
  actorLookup(actorId: string): ActorRef {
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return actorRefFromRaw(
      configCall('spot node actor lookup failed', () =>
        requireNative().spotNodeActorLookup(
          this._native,
          normalizedActorId
        ) as any
      )
    );
  }
  static remoteActorRef(targetNodeRid: RoutingId, actorId: string): ActorRef {
    return remoteActorRef(targetNodeRid, actorId);
  }
  createRemoteActor(targetNodeRid: RoutingId, actorId: string, message: MessageLike, timeoutMs = 0): ActorCreateResult {
    const normalizedTargetNodeRid = normalizeRoutingId(targetNodeRid, 'targetNodeRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    const parts = [normalizeMessageLikePayload(message)];
    return actorCreateResultFromRaw(
      configCall('remote actor create failed', () =>
        requireNative().spotNodeCreateRemoteActor(
          this._native,
          normalizedTargetNodeRid,
          normalizedActorId,
          parts,
          timeoutMs | 0
        ) as any
      )
    );
  }
  destroyActor(actor: ActorRef, timeoutMs = 0): void {
    const actorRaw = actorRefToRaw(actor);
    configCall('spot node actor destroy failed', () => {
      requireNative().spotNodeActorDestroy(
        this._native,
        actorRaw,
        timeoutMs | 0
      );
    });
  }
  onActorAdmission(handler: ActorAdmissionHandler): void {
    handlerCall('actor admission handler registration failed', () => {
      requireNative().spotNodeActorAdmissionHandler(
        this._native,
        (actorId: string, rawMessage: Buffer) => handler(actorId, messageFromNativeBuffer(rawMessage)) | 0
      );
    });
  }
  joinActor(actor: ActorRef, destNodeRid: RoutingId, destSpotRid: RoutingId, message: MessageLike, timeout?: number): Promise<Message[]>;
  joinActor(actor: ActorRef, destNodeRid: RoutingId, destSpotRid: RoutingId, message: MessageLike, callback: RequestCallback, flags?: SendFlags, timeout?: number): boolean;
  joinActor(actor: ActorRef, destNodeRid: RoutingId, destSpotRid: RoutingId, message: MessageLike, callbackOrTimeout?: RequestCallback | number, flags?: SendFlags, timeout?: number): Promise<Message[]> | boolean {
    const submit = (callback: RequestCallback, normalizedFlags: SendFlags, timeoutMs: number): boolean => {
      try {
        requireNative().spotNodeActorJoinSpot(
          this._native,
          actorRefToRaw(actor),
          normalizeRoutingId(destNodeRid, 'destNodeRid'),
          normalizeRoutingId(destSpotRid, 'destSpotRid'),
          [normalizeMessageLikePayload(message)],
          (result: number, replyParts: Buffer[] | null) => {
            callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
          },
          normalizedFlags | 0,
          timeoutMs | 0
        );
        return true;
      } catch (error) {
        const submitError = submitNativeError(error, normalizedFlags, 'actor join failed');
        if (((normalizedFlags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
          return false;
        }
        throw submitError;
      }
    };
    if (typeof callbackOrTimeout === 'function') {
      const { flags: normalizedFlags, timeoutMs } = normalizeCallbackFlagsAndTimeout(flags, timeout);
      return submit(callbackOrTimeout, normalizedFlags, timeoutMs);
    }
    const timeoutMs = typeof callbackOrTimeout === 'number' ? callbackOrTimeout : 0;
    return new Promise<Message[]>((resolve, reject) => {
      submit((result, parts) => {
        if (result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result as RequestResult, 'actor join failed'));
          return;
        }
        resolve(parts.slice());
      }, SendFlags.None, timeoutMs);
    });
  }
  leaveActor(actor: ActorRef, currentSpotRid: RoutingId, timeoutMs = 0): void {
    const actorRaw = actorRefToRaw(actor);
    const normalizedCurrentSpotRid = normalizeRoutingId(currentSpotRid, 'currentSpotRid');
    configCall('spot node actor leave failed', () => {
      requireNative().spotNodeActorLeaveSpot(
        this._native,
        actorRaw,
        normalizedCurrentSpotRid,
        timeoutMs | 0
      );
    });
  }
  /** @internal */
  unregisterSpot(spot: Spot): void {
    this._spots.delete(spot);
  }
  statusSnapshot(): SpotNodeStatus {
    const raw = configCall('spot node status snapshot failed', () =>
      requireNative().spotNodeStatusSnapshot(this._native) as {
      channelName: string;
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
    });
    if (raw.nodeRoutingId) {
      this._nodeRoutingId = RoutingId.fromBytes(raw.nodeRoutingId);
    }
    return mapSpotNodeStatus(raw, this._nodeRoutingId);
  }
  peersSnapshot(): SpotNodePeerEntry[] {
    return (configCall('spot node peers snapshot failed', () =>
      requireNative().spotNodePeersSnapshot(this._native) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapSpotNodePeerEntry(entry as any));
  }
  peersQuery(filter?: SpotNodePeerFilter): SpotNodePeerEntry[] {
    return (configCall('spot node peers query failed', () =>
      requireNative().spotNodePeersQuery(this._native, filter ?? undefined) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapSpotNodePeerEntry(entry as any));
  }
  subjectsSnapshot(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[] {
    return (configCall('spot node subjects snapshot failed', () =>
      requireNative().spotNodeSubjectsSnapshot(this._native, filter ?? undefined) as Array<Record<string, unknown>>
    ))
      .map((entry) => mapSpotNodeSubjectEntry(entry as any));
  }
  internalSocketsSnapshot(filter?: SpotNodeSocketSnapshotFilter): SpotNodeSocketSnapshotEntry[] {
    return (configCall('spot node internal socket snapshot failed', () =>
      requireNative().spotNodeInternalSocketsSnapshot(this._native, filter ?? undefined) as Array<Record<string, unknown>>
    ))
      .map((entry) => ({
        owner: entry.owner as SpotNodeSocketOwnerValue,
        ownerId: BigInt(entry.ownerId as number | bigint),
        ownerName: entry.ownerName as string,
        socketName: entry.socketName as string,
        socketType: entry.socketType as SocketTypeValue,
        autoHwmVisible: Boolean(entry.autoHwmVisible),
        snapshot: materializeMonitorSnapshot(entry.snapshot as MonitorSnapshotRaw),
      }));
  }
  spotsSnapshot(): SpotNodeSpotEntry[] {
    return (configCall('spot node spots snapshot failed', () =>
      requireNative().spotNodeSpotsSnapshot(this._native) as Array<Record<string, unknown>>
    ))
      .map((entry) => spotNodeSpotEntryFromRaw(entry as any));
  }
  actorsSnapshot(): SpotNodeActorEntry[] {
    return (configCall('spot node actors snapshot failed', () =>
      requireNative().spotNodeActorsSnapshot(this._native) as Array<Record<string, unknown>>
    ))
      .map((entry) => spotNodeActorEntryFromRaw(entry as any));
  }
  close(): void {
    if (!this._native) {
      return;
    }
    for (const spot of [...this._spots]) {
      spot.close();
    }
    closeCall('spot node close failed', () => {
      requireNative().spotNodeDestroy(this._native);
    });
    this._native = null;
  }
}

type SendInvoker = (parts: readonly MessageLike[], flags: SendFlags) => boolean;
type RequestInvoker = (
  parts: readonly MessageLike[],
  callbackOrTimeout?: RequestCallback | number,
  flagsOrTimeout?: SendFlags | number,
  maybeTimeout?: number
) => Promise<Message[]> | boolean;
type ReplyInvoker = (parts: readonly MessageLike[], flags: SendFlags) => void;

class OperationPayload {
  private readonly _parts: MessageLike[] = [];
  private _submitted = false;

  append(message: MessageLike): void {
    this.ensureOpen();
    this._parts.push(message);
  }

  ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  consume(): readonly MessageLike[] {
    this.ensureOpen();
    if (this._parts.length === 0) {
      throw new TypeError('operation requires at least one message');
    }
    this._submitted = true;
    return this._parts;
  }
}

class SendOperation implements SendOp, SendSubmitOp {
  private readonly _invoke: SendInvoker;
  private readonly _payload = new OperationPayload();
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: SendInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): SendSubmitOp {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): SendSubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): boolean {
    return this._invoke(this._payload.consume(), this._flags);
  }
}

class RequestOperation implements RequestOp, RequestSubmitOp, RequestCallbackSubmitOp {
  private readonly _invoke: RequestInvoker;
  private readonly _payload = new OperationPayload();
  private _timeoutMs = 0;
  private _flags: SendFlags = SendFlags.None;
  private _callbackMode = false;

  constructor(invoke: RequestInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): RequestSubmitOp {
    this._payload.append(message);
    return this;
  }

  timeout(timeoutMs: number): RequestSubmitOp {
    this._payload.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  flags(flags: SendFlags): RequestCallbackSubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    this._callbackMode = true;
    return this;
  }

  submitAsync(): Promise<Message[]> {
    return this._invoke(this._payload.consume(), this._timeoutMs) as Promise<Message[]>;
  }

  submit(callback: RequestCallback): boolean {
    const flags = this._callbackMode ? this._flags : SendFlags.None;
    return this._invoke(this._payload.consume(), callback, flags, this._timeoutMs) as boolean;
  }
}

class ReplyOperation implements ReplyOp, ReplySubmitOp {
  private readonly _invoke: ReplyInvoker;
  private readonly _payload = new OperationPayload();
  private _flags: SendFlags = SendFlags.None;

  constructor(invoke: ReplyInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): ReplySubmitOp {
    this._payload.append(message);
    return this;
  }

  flags(flags: SendFlags): ReplySubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consume(), this._flags);
  }
}

export class Spot extends NativeHandle {
  private static readonly CREATE_TOKEN = Symbol('Spot.create');
  private readonly _node: SpotNode;
  private constructor(node: SpotNode, token: symbol, native?: unknown) {
    if (token !== Spot.CREATE_TOKEN) {
      throw new TypeError('Spot instances must be created with SpotNode.createSpot()');
    }
    super(native ?? requireNative().spotNew(node.nativeHandle()));
    this._node = node;
  }

  /** @internal */
  static create(node: SpotNode): Spot {
    return new Spot(node, Spot.CREATE_TOKEN);
  }
  /** @internal */
  static fromNative(node: SpotNode, native: unknown): Spot {
    return new Spot(node, Spot.CREATE_TOKEN, native);
  }
  /** @internal */
  ownerNodeRoutingId(): RoutingId {
    return this._node.routingId;
  }
  setRoutingId(routingId: RoutingId): void {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    configCall('spot routing id set failed', () => {
      requireNative().handleSetRoutingId(this._native, normalizedRoutingId);
    });
  }
  get routingId(): RoutingId {
    return RoutingId.fromBytes(configCall('spot routing id get failed', () =>
      requireNative().handleGetRoutingId(this._native) as Buffer
    ));
  }
  get requestTimeout(): number {
    return readInt32Option(configCall('spot request timeout get failed', () =>
      requireNative().spotGetOption(this._native, SpotOption.REQUEST_TIMEOUT_MS) as Buffer
    ), 'requestTimeout');
  }
  set requestTimeout(value: number) {
    const buffer = int32Buffer(value, 'requestTimeout');
    configCall('spot request timeout set failed', () => {
      requireNative().spotSetOption(this._native, SpotOption.REQUEST_TIMEOUT_MS, buffer);
    });
  }
  publish(topic: string): SendOp {
    return new SendOperation((parts, opFlags) => this.publishDirect(topic, parts, opFlags));
  }
  private publishDirect(topic: string, payloadParts: readonly MessageLike[], flags: SendFlags): boolean {
    try {
      requireNative().spotPublish(
        this._native,
        validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER),
        toMessageParts(payloadParts),
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
  setSubscription(topicOrPattern: string): void {
    const normalized = validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER);
    configCall('spot subscription set failed', () => {
      requireNative().spotSubscribe(this._native, normalized);
    });
  }
  unsetSubscription(topicOrPattern: string): void {
    const normalized = validateCString(topicOrPattern, 'topicOrPattern', Number.MAX_SAFE_INTEGER);
    configCall('spot subscription unset failed', () => {
      requireNative().spotUnsubscribe(this._native, normalized);
    });
  }
  subscriptionAt(index: number): SubscriptionEntry | null {
    return configCall('spot subscription lookup failed', () =>
      requireNative().subscriptionAt(this._native, index >>> 0) as SubscriptionEntry | null
    );
  }
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
  receiveSubscriptionEvent(flags: RecvFlags = RecvFlags.None): SubscriptionEvent | null {
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().spotSubscriptionEventNoWait(this._native) as {
            routingId?: Buffer | null;
            topic: string;
            subscribed: boolean;
          } | null
        : requireNative().spotSubscriptionEvent(this._native, flags | 0) as {
            routingId?: Buffer | null;
            topic: string;
            subscribed: boolean;
          } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'spot subscription event recv failed');
    }
    return raw ? SubscriptionEvent.create(
      raw.topic,
      raw.subscribed,
      wrapRoutingId(raw.routingId ?? null)
    ) : null;
  }
  onSendReady(handler: SpotSendReadyHandler): void {
    handlerCall('spot send-ready handler registration failed', () => {
      requireNative().spotSendReadyHandler(this._native, handler);
    });
  }
  sendChannel(channelName: string): SendOp {
    return new SendOperation((parts, opFlags) => this.sendChannelDirect(channelName, parts, opFlags));
  }
  private sendChannelDirect(channelName: string, payloadParts: readonly MessageLike[], flags: SendFlags): boolean {
    try {
      requireNative().spotSendChannel(
        this._native,
        validateCString(channelName, 'channelName', Number.MAX_SAFE_INTEGER),
        toMessageParts(payloadParts),
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
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOp {
    return new SendOperation((parts, opFlags) => this.sendToSpotDirect(destNodeRid, destSpotRid, parts, opFlags));
  }
  private sendToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadParts: readonly MessageLike[], flags: SendFlags): boolean {
    try {
      requireNative().spotSendToSpot(
        this._native,
        normalizeRoutingId(destNodeRid),
        normalizeRoutingId(destSpotRid),
        toMessageParts(payloadParts),
        flags | 0
      );
      return true;
    } catch (error) {
      const submitError = submitNativeError(error, flags, 'spot sendToSpot failed');
      if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
        return false;
      }
      throw submitError;
    }
  }
  requestChannel(channelName: string): RequestOp {
    return new RequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestChannelDirect(channelName, parts, cbOrTimeout as any, opFlags as any, opTimeout)
    );
  }
  private requestChannelDirect(channelName: string, partsInput: readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const parts = toMessageParts(partsInput);
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
            callbackOrTimeout(result as RequestResult, messagesFromNativeBuffers(replyParts));
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
      try {
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
            resolve(messagesFromNativeBuffers(replyParts));
          },
          0,
          timeoutMs | 0
        );
      } catch (error) {
        releaseProgress();
        reject(submitNativeError(error, SendFlags.None, 'requestChannel failed'));
      }
    });
  }
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOp {
    return new RequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestToSpotDirect(destNodeRid, destSpotRid, parts, cbOrTimeout as any, opFlags as any, opTimeout)
    );
  }
  requestToRouter(peerRid: RoutingId): RequestOp {
    return new RequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestToRouterDirect(peerRid, parts, cbOrTimeout as any, opFlags as any, opTimeout)
    );
  }
  private requestToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, partsInput: readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const parts = toMessageParts(partsInput);
    const nodeRid = normalizeRoutingId(destNodeRid, 'destNodeRid');
    const spotRid = normalizeRoutingId(destSpotRid, 'destSpotRid');
    if (typeof callbackOrTimeout === 'function') {
      const { flags, timeoutMs } = normalizeCallbackFlagsAndTimeout(flagsOrTimeout, maybeTimeout);
      const releaseProgress = startRequestProgress(this._native, (handle) => requireNative().spotRequestProgress(handle));
      try {
        requireNative().spotRequestSpot(
          this._native,
          nodeRid,
          spotRid,
          parts,
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            callbackOrTimeout(result as RequestResult, messagesFromNativeBuffers(replyParts));
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
      const releaseProgress = startRequestProgress(this._native, (handle) => requireNative().spotRequestProgress(handle));
      try {
        requireNative().spotRequestSpot(
          this._native,
          nodeRid,
          spotRid,
          parts,
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            if (result !== RequestResult.Ok) {
              reject(requestErrorFromResult(result as RequestResult, 'requestToSpot failed'));
              return;
            }
            resolve(messagesFromNativeBuffers(replyParts));
          },
          0,
          timeoutMs | 0
        );
      } catch (error) {
        releaseProgress();
        reject(submitNativeError(error, SendFlags.None, 'requestToSpot failed'));
      }
    });
  }
  private requestToRouterDirect(peerRid: RoutingId, partsInput: readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
    const parts = toMessageParts(partsInput);
    const peer = normalizeRoutingId(peerRid, 'peerRid');
    if (typeof callbackOrTimeout === 'function') {
      const { flags, timeoutMs } = normalizeCallbackFlagsAndTimeout(flagsOrTimeout, maybeTimeout);
      const releaseProgress = startRequestProgress(this._native, (handle) => requireNative().spotRequestProgress(handle));
      try {
        requireNative().spotRequestRouter(
          this._native,
          peer,
          parts,
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            callbackOrTimeout(result as RequestResult, messagesFromNativeBuffers(replyParts));
          },
          flags | 0,
          timeoutMs | 0
        );
        return true;
      } catch (error) {
        releaseProgress();
        const submitError = submitNativeError(error, flags, 'requestToRouter failed');
        if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
          return false;
        }
        throw submitError;
      }
    }
    const timeoutMs = (typeof callbackOrTimeout === 'number' ? callbackOrTimeout : flagsOrTimeout) ?? 0;
    return new Promise<Message[]>((resolve, reject) => {
      const releaseProgress = startRequestProgress(this._native, (handle) => requireNative().spotRequestProgress(handle));
      try {
        requireNative().spotRequestRouter(
          this._native,
          peer,
          parts,
          (result: number, replyParts: Buffer[] | null) => {
            releaseProgress();
            if (result !== RequestResult.Ok) {
              reject(requestErrorFromResult(result as RequestResult, 'requestToRouter failed'));
              return;
            }
            resolve(messagesFromNativeBuffers(replyParts));
          },
          0,
          timeoutMs | 0
        );
      } catch (error) {
        releaseProgress();
        reject(submitNativeError(error, SendFlags.None, 'requestToRouter failed'));
      }
    });
  }
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOp {
    return new ReplyOperation((parts, opFlags) => this.replyToSpotInternal(destNodeRid, destSpotRid, requestSeq, parts.map(toOwnedMessage), opFlags));
  }
  replyToRouter(peerRid: RoutingId, requestSeq: bigint): ReplyOp {
    return new ReplyOperation((parts, opFlags) => this.replyToRouterInternal(peerRid, requestSeq, parts.map(toOwnedMessage), opFlags));
  }
  private replyToSpotInternal(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, parts: readonly Message[], flags: SendFlags): void {
    normalizeReplyFlags(flags);
    const normalizedDestNodeRid = normalizeRoutingId(destNodeRid);
    const normalizedDestSpotRid = normalizeRoutingId(destSpotRid);
    try {
      requireNative().spotReplySpot(
        this._native,
        normalizedDestNodeRid,
        normalizedDestSpotRid,
        requestSeq,
        parts.map((part) => part.data())
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'spot replyToSpot failed');
    }
  }
  private replyToRouterInternal(peerRid: RoutingId, requestSeq: bigint, parts: readonly Message[], flags: SendFlags): void {
    normalizeReplyFlags(flags);
    const normalizedPeerRid = normalizeRoutingId(peerRid);
    try {
      requireNative().spotReplyRouter(
        this._native,
        normalizedPeerRid,
        requestSeq,
        parts.map((part) => part.data())
      );
    } catch (error) {
      throw submitNativeError(error, flags, 'spot replyToRouter failed');
    }
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
    handlerCall('spot routed handler registration failed', () => {
      requireNative().spotRoutedHandler(this._native, (sourceRid: Buffer | null, spotRid: Buffer | null, requestSeq: bigint, parts: Buffer[]) => {
        const source = wrapRoutingId(sourceRid);
        const spot = wrapRoutingId(spotRid);
        handler(Received.create(
          messagesFromNativeBuffers(parts),
          source,
          requestSeq,
          spot,
          source
            ? {
                reply: (replyParts, flags) => {
                  if (spot) {
                    this.replyToSpotInternal(source, spot, requestSeq, replyParts, flags);
                    return;
                  }
                  this.replyToRouterInternal(source, requestSeq, replyParts, flags);
                }
              }
            : null
        )
        );
      });
    });
  }
  onDispatchEvent(handler: SpotDispatchEventHandler): void {
    handlerCall('spot dispatch handler registration failed', () => {
      requireNative().spotDispatchEventHandler(this._native, this._node.nativeHandle(), (raw: {
        event: number;
        subjectKind: number;
        subjectHandle: bigint;
        actorParts?: Array<{
          info: {
            actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
            sourceNodeRid: Buffer;
            sourceSessionRid: Buffer;
            flags: number;
          };
          message: Buffer;
          more: boolean;
        }>;
      }) => {
        const actorParts = (raw.actorParts ?? []).map((part) => actorPartFromRaw(part));
        const actorRef = actorParts[0]?.info.actor ?? null;
        let index = 0;
        handler({
          event: raw.event as SpotDispatchEvent,
          subjectKind: raw.subjectKind as SpotDispatchSubjectKind,
          timer: null,
          actorRef,
          recvActorPart(flags: RecvFlags = RecvFlags.None): ActorPart | null {
            const part = actorParts[index++] ?? null;
            if (!part && ((flags | 0) & (RecvFlags.DontWait | 0))) {
              return null;
            }
            return part;
          }
        });
      });
    });
  }
  recvActorJoin(flags: RecvFlags = RecvFlags.None): ActorJoinRequest | null {
    let raw;
    try {
      raw = requireNative().spotActorJoinRecv(this._native, flags | 0) as {
        info: {
          actor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
          sourceActor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
          targetActor?: { nodeRid: Buffer; actorId: string; generation: bigint | number };
          sourceNodeRid: Buffer;
          sourceSpotRid?: Buffer | null;
          targetNodeRid?: Buffer | null;
          targetSpotRid?: Buffer | null;
          joinEpoch?: bigint | number;
          flags: number;
          requestHandle: bigint;
        };
        message: MessageSnapshot;
      } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'actor join recv failed');
    }
    if (!raw) {
      return null;
    }
    return {
      info: actorJoinInfoFromRaw(raw.info),
      message: Message.fromSnapshot(raw.message)
    };
  }
  replyActorJoin(request: ActorJoinRequest, accepted: boolean, message: MessageLike): void {
    const rawInfo = actorJoinInfoToRaw(request.info);
    const parts = [normalizeMessageLikePayload(message)];
    try {
      requireNative().spotActorJoinReply(
        this._native,
        rawInfo,
        Boolean(accepted),
        parts
      );
    } catch (error) {
      throw submitNativeError(error, SendFlags.None, 'actor join reply failed');
    }
  }
  actorsSnapshot(): ActorRef[] {
    return (configCall('spot actors snapshot failed', () =>
      requireNative().spotActorsSnapshot(this._native) as Array<{ nodeRid: Buffer; actorId: string; generation: bigint | number }>
    ))
      .map((entry) => actorRefFromRaw(entry));
  }
  close(): void {
    if (this._native) {
      closeCall('spot close failed', () => {
        requireNative().spotDestroy(this._native);
      });
      this._native = null;
      this._node.unregisterSpot(this);
    }
  }
}

interface RawPollerEvent {
  readonly sourceKind: number;
  readonly fd: number | bigint;
  readonly userData: any;
  readonly events: number;
}

export interface PollEvent {
  readonly socket: BaseSocket | null;
  readonly fd: number | null;
  readonly timer: Timer | null;
  readonly tag: any;
  readonly events: readonly PollEventFlagValue[];
  readonly revents: readonly PollEventFlagValue[];
}

export class Poller {
  private _native: unknown | null;
  private _nextTagId = 1n;
  private readonly _registrations = new Map<bigint, {
    socket: BaseSocket | null;
    fd: number | null;
    timer: Timer | null;
    tag: any;
    events: readonly PollEventFlagValue[];
  }>();
  private readonly _socketIds = new WeakMap<BaseSocket, bigint>();
  private readonly _timerIds = new WeakMap<Timer, bigint>();
  private readonly _fdIds = new Map<number, bigint>();
  constructor() { this._native = requireNative().pollerNew(); }
  add(socket: BaseSocket, events: readonly PollEventFlagValue[], tag?: any): void;
  add(timer: Timer, tag?: any): void;
  add(item: BaseSocket | Timer, eventsOrTag?: readonly PollEventFlagValue[] | any, tag?: any): void {
    if (item instanceof Timer) {
      this.addTimerInternal(item, eventsOrTag);
      return;
    }
    this.addSocketInternal(item, flagsToMask(eventsOrTag as readonly PollEventFlagValue[]), tag, eventsOrTag as readonly PollEventFlagValue[]);
  }
  modify(socket: BaseSocket, events: readonly PollEventFlagValue[]): void {
    this.modifySocketInternal(socket, flagsToMask(events));
    const id = this._socketIds.get(socket);
    const current = id ? this._registrations.get(id) : undefined;
    if (id && current) this._registrations.set(id, { ...current, events: Object.freeze(events.slice()) });
  }
  remove(socket: BaseSocket): boolean;
  remove(timer: Timer): boolean;
  remove(item: BaseSocket | Timer): boolean {
    if (item instanceof Timer) {
      return this.removeTimerInternal(item);
    }
    return this.removeSocketInternal(item);
  }
  private registerSocket(socket: BaseSocket, tag: any, events: readonly PollEventFlagValue[]): bigint {
    const id = this._nextTagId++;
    this._socketIds.set(socket, id);
    this._registrations.set(id, {
      socket,
      fd: null,
      timer: null,
      tag: tag ?? null,
      events: Object.freeze(events.slice())
    });
    return id;
  }
  private registerFd(fd: number, tag: any, events: readonly PollEventFlagValue[]): bigint {
    const id = this._nextTagId++;
    this._fdIds.set(fd, id);
    this._registrations.set(id, {
      socket: null,
      fd,
      timer: null,
      tag: tag ?? null,
      events: Object.freeze(events.slice())
    });
    return id;
  }
  private registerTimer(timer: Timer, tag: any): bigint {
    const id = this._nextTagId++;
    this._timerIds.set(timer, id);
    this._registrations.set(id, {
      socket: null,
      fd: null,
      timer,
      tag: tag ?? null,
      events: Object.freeze([PollEventFlag.PollIn])
    });
    return id;
  }
  private eventFromRaw(raw: RawPollerEvent): PollEvent {
    const id = typeof raw.userData === 'bigint' ? raw.userData : null;
    const registration = id ? this._registrations.get(id) : undefined;
    const fd = registration?.fd ?? ((raw.sourceKind | 0) === 2 ? Number(raw.fd) : null);
    const revents = maskToFlags(raw.events | 0);
    return {
      socket: registration?.socket ?? null,
      fd,
      timer: registration?.timer ?? null,
      tag: registration?.tag ?? null,
      events: registration?.events ?? revents,
      revents
    };
  }
  private addSocketInternal(socket: BaseSocket, events: number, userData?: any, eventList: readonly PollEventFlagValue[] = maskToFlags(events)): void {
    const id = this.registerSocket(socket, userData, eventList);
    try {
      requireNative().pollerAdd(this._native, socket.nativeHandle(), id, events | 0);
    } catch (error) {
      this._registrations.delete(id);
      this._socketIds.delete(socket);
      throw createError('config', readErrno(), nativeErrorMessage(error, 'poller socket add failed'));
    }
  }
  private modifySocketInternal(socket: BaseSocket, events: number): void {
    configCall('poller socket modify failed', () => {
      requireNative().pollerModify(this._native, socket.nativeHandle(), events | 0);
    });
  }
  private removeSocketInternal(socket: BaseSocket): boolean {
    configCall('poller socket remove failed', () => {
      requireNative().pollerRemove(this._native, socket.nativeHandle());
    });
    const id = this._socketIds.get(socket);
    if (id) this._registrations.delete(id);
    this._socketIds.delete(socket);
    return true;
  }
  addFd(fd: number, events: readonly PollEventFlagValue[], tag?: any): void {
    const mask = flagsToMask(events);
    const eventList = events;
    const normalizedFd = fd | 0;
    const id = this.registerFd(normalizedFd, tag, eventList);
    try {
      requireNative().pollerAddFd(this._native, normalizedFd, id, mask);
    } catch (error) {
      this._registrations.delete(id);
      this._fdIds.delete(normalizedFd);
      throw createError('config', readErrno(), nativeErrorMessage(error, 'poller fd add failed'));
    }
  }
  modifyFd(fd: number, events: readonly PollEventFlagValue[]): void {
    const mask = flagsToMask(events);
    const normalizedFd = fd | 0;
    configCall('poller fd modify failed', () => {
      requireNative().pollerModifyFd(this._native, normalizedFd, mask);
    });
    const id = this._fdIds.get(normalizedFd);
    const current = id ? this._registrations.get(id) : undefined;
    if (id && current) {
      this._registrations.set(id, {
        ...current,
        events: Object.freeze(events.slice())
      });
    }
  }
  removeFd(fd: number): boolean {
    const normalizedFd = fd | 0;
    configCall('poller fd remove failed', () => {
      requireNative().pollerRemoveFd(this._native, normalizedFd);
    });
    const id = this._fdIds.get(normalizedFd);
    if (id) this._registrations.delete(id);
    this._fdIds.delete(normalizedFd);
    return true;
  }
  private addTimerInternal(timer: Timer, userData?: any): void {
    const id = this.registerTimer(timer, userData);
    try {
      requireNative().pollerAddTimer(this._native, timer.nativeHandle(), id);
    } catch (error) {
      this._registrations.delete(id);
      this._timerIds.delete(timer);
      throw createError('config', readErrno(), nativeErrorMessage(error, 'poller timer add failed'));
    }
  }
  private removeTimerInternal(timer: Timer): boolean {
    configCall('poller timer remove failed', () => {
      requireNative().pollerRemoveTimer(this._native, timer.nativeHandle());
    });
    const id = this._timerIds.get(timer);
    if (id) this._registrations.delete(id);
    this._timerIds.delete(timer);
    return true;
  }
  get size(): number {
    return configCall('poller size failed', () =>
      requireNative().pollerSize(this._native) as number
    );
  }
  wait(timeoutMs: number): PollEvent | null {
    try {
      const raw = requireNative().pollerWait(this._native, timeoutMs | 0) as RawPollerEvent | null;
      return raw ? this.eventFromRaw(raw) : null;
    } catch (error) {
      const message = error instanceof Error && error.message ? error.message : String(error);
      if (/Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
        return null;
      }
      throw recvNativeError(error, RecvFlags.None, 'poller wait failed');
    }
  }
  waitAll(maxEvents: number, timeoutMs: number): PollEvent[] {
    try {
      return (requireNative().pollerWaitAll(this._native, maxEvents | 0, timeoutMs | 0) as RawPollerEvent[])
        .map((raw) => this.eventFromRaw(raw));
    } catch (error) {
      const message = error instanceof Error && error.message ? error.message : String(error);
      if (/Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
        return [];
      }
      throw recvNativeError(error, RecvFlags.None, 'poller waitAll failed');
    }
  }
  destroy(): void {
    if (this._native) {
      closeCall('poller close failed', () => {
        requireNative().pollerDestroy(this._native);
      });
      this._native = null;
    }
  }
  close(): void { this.destroy(); }
}

export class Timer extends NativeHandle {
  private static readonly CREATE_TOKEN = Symbol('Timer.fromNative');
  constructor();
  /** @internal */
  constructor(token: symbol, native: unknown);
  constructor(token?: symbol, native?: unknown) {
    super(token === Timer.CREATE_TOKEN ? native : requireNative().timerNew());
  }
  static fromSpot(spot: Spot): Timer {
    return new Timer(Timer.CREATE_TOKEN, configCall('spot timer creation failed', () =>
      requireNative().spotTimerNew(spot.nativeHandle())
    ));
  }
  start(intervalNs: bigint, repeatCount: bigint): void {
    configCall('timer start failed', () => {
      requireNative().timerStart(this._native, intervalNs, repeatCount);
    });
  }
  stop(): void {
    configCall('timer stop failed', () => {
      requireNative().timerStop(this._native);
    });
  }
  recv(): bigint | null {
    const flags = RecvFlags.None;
    try {
      return requireNative().timerRecv(this._native, flags | 0) as bigint | null;
    } catch (error) {
      const recvError = recvNativeError(error, flags, 'timer recv failed');
      if (recvError.result === RecvResult.NoData) return null;
      throw recvError;
    }
  }
  onFire(handler: TimerHandler): void {
    handlerCall('timer handler registration failed', () => {
      requireNative().timerHandler(this._native, (fireCount: bigint) => handler(this, fireCount));
    });
  }
  close(): void {
    if (this._native) {
      closeCall('timer close failed', () => {
        requireNative().timerDestroy(this._native);
      });
      this._native = null;
    }
  }
}

export class Stopwatch extends NativeHandle {
  constructor() { super(requireNative().stopwatchStart()); }
  intermediate(): number { return requireNative().stopwatchIntermediate(this._native) as number; }
  stop(): number { return requireNative().stopwatchStop(this._native) as number; }
  close(): void { this._native = null; }
}

export class Thread {
  private readonly _state = new Int32Array(new SharedArrayBuffer(4));
  private readonly _worker: Worker;
  constructor(handler: () => void) {
    if (typeof handler !== 'function') {
      throw new TypeError('handler must be a function');
    }
    const source = `
      const { workerData } = require('node:worker_threads');
      const state = new Int32Array(workerData.state);
      (async () => {
        try {
          const handler = (${handler.toString()});
          await handler();
          Atomics.store(state, 0, 1);
        } catch {
          Atomics.store(state, 0, 2);
        } finally {
          Atomics.notify(state, 0);
        }
      })();
    `;
    this._worker = new Worker(source, {
      eval: true,
      workerData: { state: this._state.buffer }
    });
    this._worker.on('error', () => {
      Atomics.store(this._state, 0, 2);
      Atomics.notify(this._state, 0);
    });
    this._worker.on('exit', (code) => {
      if (code !== 0 && Atomics.load(this._state, 0) === 0) {
        Atomics.store(this._state, 0, 2);
        Atomics.notify(this._state, 0);
      }
    });
  }
  join(): void {
    while (Atomics.load(this._state, 0) === 0) {
      Atomics.wait(this._state, 0, 0);
    }
    if (Atomics.load(this._state, 0) === 2) {
      throw new Error('thread handler failed');
    }
  }
}

export class AtomicCounter {
  private _native: unknown | null;
  constructor(initialValue = 0) {
    this._native = requireNative().atomicCounterNew();
    if ((initialValue | 0) !== 0) {
      this.set(initialValue);
    }
  }
  set(value: number): void { requireNative().atomicCounterSet(this._native, value | 0); }
  inc(): number { return requireNative().atomicCounterInc(this._native) as number; }
  dec(): number { return requireNative().atomicCounterDec(this._native) as number; }
  value(): number { return requireNative().atomicCounterValue(this._native) as number; }
  close(): void {
    if (this._native) {
      requireNative().atomicCounterDestroy(this._native);
      this._native = null;
    }
  }
}

export function version(): [number, number, number] {
  return requireNative().version() as [number, number, number];
}

export function strerror(code: number): string { return requireNative().strerror(code) as string; }
export function has(capability: string): boolean { return requireNative().has(validateCString(capability, 'capability', Number.MAX_SAFE_INTEGER)) as boolean; }
export function proxy(frontend: BaseSocket, backend: BaseSocket, capture?: BaseSocket): void {
  if (capture === null) {
    throw new TypeError('capture must be a socket or undefined');
  }
  configCall('proxy failed', () => {
    requireNative().proxy(frontend.nativeHandle(), backend.nativeHandle(), capture ? capture.nativeHandle() : null);
  });
}
export function proxySteerable(frontend: BaseSocket, backend: BaseSocket, capture: BaseSocket | null, control: BaseSocket): void {
  configCall('proxySteerable failed', () => {
    requireNative().proxySteerable(frontend.nativeHandle(), backend.nativeHandle(), capture ? capture.nativeHandle() : null, control.nativeHandle());
  });
}
export function sleep(seconds: number): void { requireNative().sleep(seconds | 0); }
export function multipartClose(parts: Message[]): void { for (const part of parts) part.close(); }
