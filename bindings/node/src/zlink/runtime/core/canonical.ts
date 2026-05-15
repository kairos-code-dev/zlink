// SPDX-License-Identifier: MPL-2.0

import { randomBytes } from 'node:crypto';
import { Worker } from 'node:worker_threads';
import { requireNative } from '../native/native';
import { normalizeBufferLike, type BufferLike } from '../../contracts/core/buffer_like';
import {
  Message,
  Received,
  RoutingId,
  TopicMessage,
  SubscriptionEvent,
  type MessageLike,
  type MessageSnapshot
} from '../../contracts/messaging/message';
import { validateCString } from './validation';
import {
  SocketType as NativeSocketType, SocketOption, SendFlags, RecvFlags, RidDuplicatePolicy,
  PollEventFlag, type PollEventFlagValue, type RidDuplicatePolicy as RidDuplicatePolicyValue
} from '../../contracts/enums/socket_constants';
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
} from '../../contracts/errors/errors';

import {
  ContextOption,
  SpotNodeOption,
  SpotOption,
  MonitorState,
  MonitorSnapshotDetail,
  AutoHwmProfile,
  MonitorSourceKind,
  SpotDispatchEvent,
  SpotDispatchSubjectKind,
  MonitorEventType,
  MonitorEvent,
  AutoConnectType,
  ServiceRole,
  ServiceKind,
  SpotRole,
  SpotPeerSource,
  SpotPeerState,
  SpotNodeState,
  SpotNodeMode,
  SpotNodeSocketOwner,
  SocketType,
  RegistryState,
  TopologySource,
  TopologyState,
  wrapRoutingId,
  type AutoHwmProfileValue,
  type MonitorSourceKindValue,
  type SpotDispatchSubjectKind as SpotDispatchSubjectKindValue,
  type MonitorSnapshot,
  type MonitorSnapshotRaw,
  type MonitorEventValueRaw,
  type ServiceRoleValue,
  type ServiceKindValue,
  type SpotRoleValue,
  type SpotPeerSourceValue,
  type SpotPeerStateValue,
  type SpotNodeStateValue,
  type SpotNodeSocketOwnerValue,
  type SocketTypeValue,
  type RegistryStateValue,
  type TopologySourceValue,
  type TopologyStateValue,
  type SpotNodeModeValue,
  type MemberPeerEntry,
  type RegistryTopologyEntry,
  type RegistryServiceSummaryEntry,
  type RegistryStatus,
  type SpotNodeStatus,
  type SpotNodePeerEntry,
  type SpotNodeSubjectEntry,
  type SpotNodeSocketSnapshotFilter,
  type SpotNodeSocketSnapshotEntry,
  type RegistryServiceSummaryFilter,
  type RegistryTopologyFilter,
  type SpotNodePeerFilter,
  type SpotNodeSubjectFilter,
  type SubscriptionEntry,
  type SocketSendReadyHandler,
  type StreamPacketHandler,
  type SocketMonitorHandler,
  type SpotSendReadyHandler,
  type SpotRoutedHandler,
  type ActorRef,
  type ActorRoute,
  type ActorRecvInfo,
  type ActorJoinInfo,
  type ActorPart,
  type ActorJoinRequest,
  type ActorJoinResult,
  type ActorLookupResult,
  type SpotActorLifecycleInfo,
  type ActorJoinHandler,
  type ActorLookupHandler,
  type ActorLifecycleHandler,
  type ReplyHandler,
  type SpotNodeSpotEntry,
  type SpotNodeActorEntry,
  type SpotDispatchInfo,
  type SpotDispatchEventHandler,
  type TimerHandler,
  type RequestCallback,
  type SendOp,
  type SendSubmitOp,
  type RequestOp,
  type RequestSubmitOp,
  type RequestCallbackSubmitOp,
  type ReplyOp,
  type ReplySubmitOp,
  type ActorJoinOp,
  type ActorJoinSubmitOp,
  type ActorJoinCallbackSubmitOp,
  type ActorJoinReplyOp,
  type ActorLeaveOp,
  type ActorDestroyOp,
  type ActorLookupOp,
  type ActorBindOp,
  type ActorUnbindOp,
} from '../../contracts/service/models';

export type { BufferLike, MessageLike };
export type { PollEventFlagValue, RidDuplicatePolicy as RidDuplicatePolicyValue } from '../../contracts/enums/socket_constants';
export {
  AutoHwmProfile,
  MonitorSourceKind,
  SpotDispatchEvent,
  SpotDispatchSubjectKind,
  MonitorEventType,
  MonitorEvent,
  AutoConnectType,
  ServiceRole,
  ServiceKind,
  SpotRole,
  SpotPeerSource,
  SpotPeerState,
  SpotNodeState,
  SpotNodeMode,
  SpotNodeSocketOwner,
  SocketType,
  RegistryState,
  TopologySource,
  TopologyState,
} from '../../contracts/service/models';
export type {
  AutoHwmProfileValue,
  MonitorSourceKindValue,
  ServiceRoleValue,
  ServiceKindValue,
  SpotRoleValue,
  SpotPeerSourceValue,
  SpotPeerStateValue,
  SpotNodeStateValue,
  SpotNodeSocketOwnerValue,
  SocketTypeValue,
  RegistryStateValue,
  TopologySourceValue,
  TopologyStateValue,
  SpotNodeModeValue,
  MemberPeerEntry,
  RegistryTopologyEntry,
  RegistryServiceSummaryEntry,
  RegistryStatus,
  SpotNodeStatus,
  SpotNodePeerEntry,
  SpotNodeSubjectEntry,
  SpotNodeSocketSnapshotFilter,
  SpotNodeSocketSnapshotEntry,
  RegistryServiceSummaryFilter,
  RegistryTopologyFilter,
  SpotNodePeerFilter,
  SpotNodeSubjectFilter,
  SubscriptionEntry,
  SocketSendReadyHandler,
  StreamPacketHandler,
  SocketMonitorHandler,
  SpotSendReadyHandler,
  SpotRoutedHandler,
  ActorRef,
  ActorRoute,
  ActorRecvInfo,
  ActorJoinInfo,
  ActorPart,
  ActorJoinRequest,
  ActorJoinResult,
  ActorLookupResult,
  SpotActorLifecycleInfo,
  ActorJoinHandler,
  ActorLookupHandler,
  ActorLifecycleHandler,
  ReplyHandler,
  SpotNodeSpotEntry,
  SpotNodeActorEntry,
  SpotDispatchInfo,
  SpotDispatchEventHandler,
  TimerHandler,
  RequestCallback,
  SendOp,
  SendSubmitOp,
  RequestOp,
  RequestSubmitOp,
  RequestCallbackSubmitOp,
  ReplyOp,
  ReplySubmitOp,
  ActorJoinOp,
  ActorJoinSubmitOp,
  ActorJoinCallbackSubmitOp,
  ActorJoinReplyOp,
  ActorLeaveOp,
  ActorDestroyOp,
  ActorLookupOp,
  ActorBindOp,
  ActorUnbindOp,
} from '../../contracts/service/models';
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
  return {
    nodeRid: RoutingId.fromBytes(normalizeRoutingId(targetNodeRid, 'targetNodeRid')),
    actorId: validateCString(actorId, 'actorId', 255),
    generation: 0n
  };
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
    if (message.length === 1) {
      return normalizeMessageLikePayload(message[0]);
    }
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

interface ActorJoinResultRaw {
  result: number;
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  joinedSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
}

interface ActorLookupResultRaw {
  result: number;
  actor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  flags: number;
}

interface SpotActorLifecycleInfoRaw {
  previousActor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  currentActor: { nodeRid: Buffer; actorId: string; generation: bigint | number };
  previousSpotRid?: Buffer | null;
  currentSpotRid?: Buffer | null;
  joinEpoch?: bigint | number;
  flags: number;
}

function actorJoinResultFromRaw(raw: ActorJoinResultRaw | null): ActorJoinResult {
  if (!raw) {
    return {
      result: RequestResult.InternalError,
      actor: { nodeRid: RoutingId.fromBytes(Buffer.alloc(1)), actorId: '', generation: 0n },
      joinedSpotRid: RoutingId.fromBytes(Buffer.alloc(1)),
      joinEpoch: 0n,
      flags: 0,
    };
  }
  return {
    result: raw.result as RequestResult,
    actor: actorRefFromRaw(raw.actor),
    joinedSpotRid: (wrapRoutingId(raw.joinedSpotRid ?? null) as RoutingId) ?? RoutingId.fromBytes(Buffer.alloc(1)),
    joinEpoch: BigInt(raw.joinEpoch ?? 0),
    flags: raw.flags | 0,
  };
}

function actorLookupResultFromRaw(raw: ActorLookupResultRaw): ActorLookupResult {
  return {
    result: raw.result as RequestResult,
    actor: actorRefFromRaw(raw.actor),
    flags: raw.flags | 0,
  };
}

function spotActorLifecycleInfoFromRaw(raw: SpotActorLifecycleInfoRaw): SpotActorLifecycleInfo {
  return {
    previousActor: actorRefFromRaw(raw.previousActor),
    currentActor: actorRefFromRaw(raw.currentActor),
    previousSpotRid: wrapRoutingId(raw.previousSpotRid ?? null),
    currentSpotRid: wrapRoutingId(raw.currentSpotRid ?? null),
    joinEpoch: BigInt(raw.joinEpoch ?? 0),
    flags: raw.flags | 0,
  };
}

function invokeActorJoin(
  nodeHandle: unknown,
  actor: ActorRef,
  destNodeRid: RoutingId,
  destSpotRid: RoutingId,
  spotHandle: unknown | null,
  parts: readonly MessageLike[],
  callback: ActorJoinHandler,
  flags: SendFlags,
  timeoutMs: number,
): boolean {
  const releaseProgress = spotHandle
    ? startRequestProgress(spotHandle, (handle) => requireNative().spotRequestProgress(handle))
    : null;
  try {
    requireNative().spotNodeActorJoinSpot(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeRoutingId(destNodeRid, 'destNodeRid'),
      normalizeRoutingId(destSpotRid, 'destSpotRid'),
      toMessageParts(parts),
      (rawResult: ActorJoinResultRaw | null, replyParts: Buffer[] | null) => {
        if (releaseProgress) releaseProgress();
        callback(actorJoinResultFromRaw(rawResult), messagesFromNativeBuffers(replyParts));
      },
      flags | 0,
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    if (releaseProgress) releaseProgress();
    const submitError = submitNativeError(error, flags, 'actor join failed');
    if (((flags | 0) & (SendFlags.DontWait | 0)) && submitError.result === SubmitResult.Backpressured) {
      return false;
    }
    throw submitError;
  }
}

function invokeActorLeave(
  nodeHandle: unknown,
  actor: ActorRef,
  currentSpotRid: RoutingId,
  callback: ReplyHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().spotNodeActorLeaveSpot(
      nodeHandle,
      actorRefToRaw(actor),
      normalizeRoutingId(currentSpotRid, 'currentSpotRid'),
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'actor leave failed');
  }
}

function invokeActorDestroy(
  nodeHandle: unknown,
  actorRaw: ReturnType<typeof actorRefToRaw>,
  callback: ReplyHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().spotNodeActorDestroy(
      nodeHandle,
      actorRaw,
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'actor destroy failed');
  }
}

function invokeRemoteActorGetRef(
  nodeHandle: unknown,
  targetNodeRid: Buffer,
  actorId: string,
  callback: ActorLookupHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().remoteActorGetRef(
      nodeHandle,
      targetNodeRid,
      actorId,
      (raw: ActorLookupResultRaw) => callback(actorLookupResultFromRaw(raw)),
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'remote actor lookup failed');
  }
}

function invokeActorSendBoundSession(
  nodeHandle: unknown,
  actor: ActorRef,
  parts: readonly MessageLike[],
  flags: SendFlags,
): boolean {
  try {
    requireNative().spotNodeActorSendBoundSessionMsg(
      nodeHandle,
      actorRefToRaw(actor),
      toMessageParts(parts),
      flags | 0,
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

function invokeStreamBindActor(
  streamHandle: unknown,
  sessionRid: Buffer,
  actorRaw: ReturnType<typeof actorRefToRaw>,
  callback: ReplyHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().streamBindActor(
      streamHandle,
      sessionRid,
      actorRaw,
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'stream actor bind failed');
  }
}

function invokeStreamUnbindActor(
  streamHandle: unknown,
  sessionRid: Buffer,
  actorId: string,
  callback: ReplyHandler,
  timeoutMs: number,
): boolean {
  try {
    requireNative().streamUnbindActor(
      streamHandle,
      sessionRid,
      actorId,
      (result: number, replyParts: Buffer[] | null) => {
        callback(result as RequestResult, messagesFromNativeBuffers(replyParts));
      },
      timeoutMs | 0,
    );
    return true;
  } catch (error) {
    throw submitNativeError(error, SendFlags.None, 'stream actor unbind failed');
  }
}

function invokeStreamSendBoundActor(
  streamHandle: unknown,
  sessionRid: Buffer,
  actorId: string,
  parts: readonly MessageLike[],
  flags: SendFlags,
): boolean {
  try {
    requireNative().streamSendBoundActorPart(
      streamHandle,
      sessionRid,
      actorId,
      toMessageParts(parts),
      flags | 0,
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
  reply?: (requestSeq: bigint, parts: readonly Message[], flags: SendFlags) => void,
  send?: (parts: readonly Message[], flags: SendFlags) => boolean
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
      : null,
    send
      ? {
          send(parts: readonly Message[], flags: SendFlags): boolean {
            return send(parts, flags);
          }
        }
      : null
  );
}

function materializeReceivedInto(
  target: Received,
  raw: {
    parts: MessageSnapshot[];
    routingId?: Buffer | null;
    requestSeq?: bigint | null;
    spotRid?: Buffer | null;
  },
  reply?: (requestSeq: bigint, parts: readonly Message[], flags: SendFlags) => void,
  send?: (parts: readonly Message[], flags: SendFlags) => boolean
): void {
  const requestSeq = raw.requestSeq ?? null;
  (target as Received & {
    _replace: (
      parts: Message[],
      routingId: RoutingId | null,
      requestSeq: bigint | null,
      spotRid: RoutingId | null,
      replyContext: unknown,
      sendContext: unknown
    ) => void;
  })._replace(
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
      : null,
    send
      ? {
          send(parts: readonly Message[], flags: SendFlags): boolean {
            return send(parts, flags);
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

function adoptTopicMessage(result: TopicMessage, raw: {
  topic: string;
  parts: MessageSnapshot[];
  routingId?: Buffer | null;
}): void {
  result.adoptFrom(materializeTopicMessage(raw));
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
  send(): SendOp {
    return new SendOperation((parts, flags) => this.sendDirect(parts, flags));
  }
  protected sendDirect(payloadOrParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
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
  publish(topic: string): SendOp {
    return new SendOperation((parts, flags) => this.publishDirect(topic, parts, flags));
  }
  protected publishDirect(topic: string, payload: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalizedTopic = validateCString(topic, 'topic', Number.MAX_SAFE_INTEGER);
    const normalized = payload.map((part, index) =>
      part instanceof Message ? part.data() : normalizeBufferLike(part, `parts[${index}]`)
    );
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
  recv(resultOrFlags: Received | RecvFlags = RecvFlags.None,
      flags: RecvFlags = RecvFlags.None): Received | null | boolean {
    const result = resultOrFlags instanceof Received ? resultOrFlags : null;
    const recvFlags: RecvFlags = result ? flags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((recvFlags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null
        : requireNative().socketRecvMessage(this.nativeHandle(), recvFlags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null; requestSeq?: bigint | null } | null;
    } catch (error) {
      throw recvNativeError(error, recvFlags, 'recv failed');
    }
    if (raw == null) return result ? false : null;
    if (!result) return materializeReceived(raw);
    materializeReceivedInto(result, raw);
    return true;
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
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  subscribe(resultOrFlags: TopicMessage | RecvFlags = RecvFlags.None,
            maybeFlags: RecvFlags = RecvFlags.None): TopicMessage | null | boolean {
    const hasResult = resultOrFlags instanceof TopicMessage;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTrySubscribeMessage(this.nativeHandle()) as { topic: string; parts: MessageSnapshot[]; routingId?: Buffer | null } | null
        : requireNative().socketSubscribeMessage(this.nativeHandle(), flags | 0) as { topic: string; parts: MessageSnapshot[]; routingId?: Buffer | null } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscribe failed');
    }
    if (!raw) {
      return hasResult ? false : null;
    }
    if (hasResult) {
      adoptTopicMessage(resultOrFlags, raw);
      return true;
    }
    return materializeTopicMessage(raw);
  }
}

class RoutedMessageSocket extends ConnectableSocket {
  send(routingId: RoutingId): SendOp {
    return new SendOperation((parts, flags) => this.sendDirect(routingId, parts, flags));
  }
  protected sendDirect(routingId: RoutingId, payload: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalizedRoutingId = normalizeRoutingId(routingId);
    return this.sendDirectRaw(normalizedRoutingId, payload, flags);
  }
  protected sendDirectRaw(routingId: Buffer, payload: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
    const normalized = normalizeMessageLikePayload(payload);
    if ((flags | 0) & (SendFlags.DontWait | 0)) {
      let result;
      try {
        result = Array.isArray(normalized)
          ? requireNative().socketSendRoutingNoWaitResultParts(this.nativeHandle(), routingId, normalized) as number
          : requireNative().socketSendRoutingNoWaitResult(this.nativeHandle(), routingId, normalized) as number;
      } catch (error) {
        throw submitNativeError(error, flags, 'send failed');
      }
      if (result === SubmitResult.Ok) return true;
      if (result === SubmitResult.Backpressured) return false;
      throw submitErrorFromResult(result as SubmitResult, 'send failed');
    }
    const parts = Array.isArray(normalized)
      ? [routingId, ...normalized]
      : [routingId, normalized];
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
  recv(resultOrFlags: Received | RecvFlags = RecvFlags.None,
      flags: RecvFlags = RecvFlags.None): Received | null | boolean {
    const result = resultOrFlags instanceof Received ? resultOrFlags : null;
    const recvFlags: RecvFlags = result ? flags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((recvFlags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().routerRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null; spotRid?: Buffer | null; requestSeq?: bigint | null } | null
        : requireNative().routerRecvMessage(this.nativeHandle(), recvFlags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null; spotRid?: Buffer | null; requestSeq?: bigint | null } | null;
    } catch (error) {
      throw recvNativeError(error, recvFlags, 'recv failed');
    }
    if (raw == null) return result ? false : null;
    const send = (parts: readonly Message[], sendFlags: SendFlags) => {
        if (!raw.routingId) {
          throw submitErrorFromResult(SubmitResult.InvalidState, 'missing routed send target');
        }
        if (raw.spotRid) {
          return (this as unknown as RouterSocket).sendToSpotDirect(
            RoutingId.fromBytes(raw.routingId),
            RoutingId.fromBytes(raw.spotRid),
            parts,
            sendFlags
          );
        }
        return this.sendDirectRaw(raw.routingId, parts, sendFlags);
      };
    if (!result) return materializeReceived(raw, undefined, send);
    materializeReceivedInto(result, raw, undefined, send);
    return true;
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
  receiveSubscriptionEvent(result: SubscriptionEvent, flags?: RecvFlags): boolean;
  receiveSubscriptionEvent(resultOrFlags: SubscriptionEvent | RecvFlags = RecvFlags.None,
                           maybeFlags: RecvFlags = RecvFlags.None): SubscriptionEvent | null | boolean {
    const hasResult = resultOrFlags instanceof SubscriptionEvent;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketTrySubscriptionEvent(this.nativeHandle()) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null
        : requireNative().socketSubscriptionEvent(this.nativeHandle(), flags | 0) as { routingId?: Buffer | null; topic: string; subscribed: boolean } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscription event recv failed');
    }
    if (!raw) {
      return hasResult ? false : null;
    }
    const event = SubscriptionEvent.create(raw.topic, raw.subscribed, wrapRoutingId(raw.routingId ?? null));
    if (hasResult) {
      resultOrFlags.adoptFrom(event);
      return true;
    }
    return event;
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
  request(): RequestOp {
    return new RequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestDirect(parts, cbOrTimeout as any, opFlags as any, opTimeout)
    );
  }
  private requestDirect(
    payloadOrParts: readonly MessageLike[],
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
  request(peerRid: RoutingId): RequestOp {
    return new RequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestDirect(peerRid, parts, cbOrTimeout as any, opFlags as any, opTimeout)
    );
  }
  private requestDirect(
    peerRid: RoutingId,
    payloadOrParts: readonly MessageLike[],
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
  sendToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): SendOp {
    return new SendOperation((parts, opFlags) => this.sendToSpotDirect(destNodeRid, destSpotRid, parts, opFlags));
  }
  /** @internal */
  sendToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
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
  requestToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId): RequestOp {
    return new RequestOperation((parts, cbOrTimeout, opFlags, opTimeout) =>
      this.requestToSpotDirect(destNodeRid, destSpotRid, parts, cbOrTimeout as any, opFlags as any, opTimeout)
    );
  }
  private requestToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, payloadOrParts: readonly MessageLike[], callbackOrTimeout?: RequestCallback | number, flagsOrTimeout?: SendFlags | number, maybeTimeout?: number): Promise<Message[]> | boolean {
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
  replyToSpot(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint): ReplyOp {
    return new ReplyOperation((parts, opFlags) => this.replyToSpotDirect(destNodeRid, destSpotRid, requestSeq, parts, opFlags));
  }
  private replyToSpotDirect(destNodeRid: RoutingId, destSpotRid: RoutingId, requestSeq: bigint, payloadOrParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
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
  reply(peerRid: RoutingId, requestSeq: bigint): ReplyOp {
    return new ReplyOperation((parts, opFlags) => this.replyDirect(peerRid, requestSeq, parts, opFlags));
  }
  private replyDirect(peerRid: RoutingId, requestSeq: bigint, payloadOrParts: readonly MessageLike[], flags: SendFlags = SendFlags.None): void {
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
  join(spot: Spot): ActorJoinOp {
    if (!(spot instanceof Spot)) {
      throw new TypeError('spot must be a Spot');
    }
    return new ActorJoinOperation((parts, callback, flags, timeoutMs) =>
      invokeActorJoin(
        this._native,
        this.ref(),
        spot.ownerNodeRoutingId(),
        spot.routingId,
        spot.nativeHandle(),
        parts,
        callback,
        flags,
        timeoutMs,
      ),
    );
  }
  leave(spot: Spot): ActorLeaveOp {
    if (!(spot instanceof Spot)) {
      throw new TypeError('spot must be a Spot');
    }
    const actorRef = this.ref();
    const spotRid = spot.routingId;
    return new ActorLeaveOperation((callback, timeoutMs) =>
      invokeActorLeave(this._native, actorRef, spotRid, callback, timeoutMs),
    );
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
  sendBoundSession(): SendOp {
    const node = this._native;
    const ref = this.ref();
    return new SendOperation((parts, flags) => invokeActorSendBoundSession(node, ref, parts, flags));
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
  send(routingId: RoutingId): SendOp {
    return new SendOperation((parts, flags) => this.sendDirect(routingId, parts, flags));
  }
  private sendDirect(routingId: RoutingId, payload: readonly MessageLike[], flags: SendFlags = SendFlags.None): boolean {
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
  recv(result: Received, flags?: RecvFlags): boolean;
  recv(resultOrFlags: Received | RecvFlags = RecvFlags.None,
      flags: RecvFlags = RecvFlags.None): Received | null | boolean {
    const result = resultOrFlags instanceof Received ? resultOrFlags : null;
    const recvFlags: RecvFlags = result ? flags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((recvFlags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().socketRecvMessageNoWait(this.nativeHandle()) as { parts: MessageSnapshot[]; routingId?: Buffer | null } | null
        : requireNative().socketRecvMessage(this.nativeHandle(), recvFlags | 0) as { parts: MessageSnapshot[]; routingId?: Buffer | null } | null;
    } catch (error) {
      throw recvNativeError(error, recvFlags, 'recv failed');
    }
    if (raw == null) return result ? false : null;
    const send = (parts: readonly Message[], sendFlags: SendFlags) => {
        if (!raw.routingId) {
          throw submitErrorFromResult(SubmitResult.InvalidState, 'missing routed send target');
        }
        return this.sendDirect(RoutingId.fromBytes(raw.routingId), parts, sendFlags);
      };
    if (!result) return materializeReceived(raw, undefined, send);
    materializeReceivedInto(result, raw, undefined, send);
    return true;
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
  bindActor(sessionRid: RoutingId, actor: ActorRef): ActorBindOp {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const actorRaw = actorRefToRaw(actor);
    return new ActorBindOperation((callback, timeoutMs) =>
      invokeStreamBindActor(handle, normalizedSessionRid, actorRaw, callback, timeoutMs),
    );
  }
  unbindActor(sessionRid: RoutingId, actorId: string): ActorUnbindOp {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return new ActorUnbindOperation((callback, timeoutMs) =>
      invokeStreamUnbindActor(handle, normalizedSessionRid, normalizedActorId, callback, timeoutMs),
    );
  }
  sendBoundActor(sessionRid: RoutingId, actorId: string): SendOp {
    const handle = this.nativeHandle();
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return new SendOperation((parts, flags) =>
      invokeStreamSendBoundActor(handle, normalizedSessionRid, normalizedActorId, parts, flags),
    );
  }
  boundActors(sessionRid: RoutingId): ActorRef[] {
    const normalizedSessionRid = normalizeRoutingId(sessionRid, 'sessionRid');
    return (configCall('stream bound actors snapshot failed', () =>
      requireNative().streamBoundActors(this.nativeHandle(), normalizedSessionRid) as Array<{ nodeRid: Buffer; actorId: string; generation: bigint | number }>
    )).map((entry) => actorRefFromRaw(entry));
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
  remoteActorGetRef(targetNodeRid: RoutingId, actorId: string): ActorLookupOp {
    const node = this._native;
    const normalizedNodeRid = normalizeRoutingId(targetNodeRid, 'targetNodeRid');
    const normalizedActorId = validateCString(actorId, 'actorId', 255);
    return new ActorLookupOperation((callback, timeoutMs) =>
      invokeRemoteActorGetRef(node, normalizedNodeRid, normalizedActorId, callback, timeoutMs),
    );
  }
  destroyActor(actor: ActorRef): ActorDestroyOp {
    const node = this._native;
    const actorRaw = actorRefToRaw(actor);
    return new ActorDestroyOperation((callback, timeoutMs) =>
      invokeActorDestroy(node, actorRaw, callback, timeoutMs),
    );
  }
  joinActor(actor: ActorRef, destNodeRid: RoutingId, destSpotRid: RoutingId): ActorJoinOp {
    const node = this._native;
    return new ActorJoinOperation((parts, callback, flags, timeoutMs) =>
      invokeActorJoin(node, actor, destNodeRid, destSpotRid, null, parts, callback, flags, timeoutMs),
    );
  }
  leaveActor(actor: ActorRef, currentSpotRid: RoutingId): ActorLeaveOp {
    const node = this._native;
    return new ActorLeaveOperation((callback, timeoutMs) =>
      invokeActorLeave(node, actor, currentSpotRid, callback, timeoutMs),
    );
  }
  sendBoundSessionMsg(actor: ActorRef): SendOp {
    const node = this._native;
    return new SendOperation((parts, flags) => invokeActorSendBoundSession(node, actor, parts, flags));
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

type ActorJoinInvoker = (
  parts: readonly MessageLike[],
  callback: ActorJoinHandler,
  flags: SendFlags,
  timeoutMs: number,
) => boolean;

class ActorJoinOperation implements ActorJoinOp, ActorJoinSubmitOp, ActorJoinCallbackSubmitOp {
  private readonly _invoke: ActorJoinInvoker;
  private readonly _payload = new OperationPayload();
  private _flags: SendFlags = SendFlags.None;
  private _timeoutMs = 0;
  private _callbackMode = false;

  constructor(invoke: ActorJoinInvoker) {
    this._invoke = invoke;
  }

  message(message: MessageLike): this {
    this._payload.append(message);
    return this;
  }

  timeout(timeoutMs: number): this {
    this._payload.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  flags(flags: SendFlags): ActorJoinCallbackSubmitOp {
    this._payload.ensureOpen();
    this._flags = flags;
    this._callbackMode = true;
    return this;
  }

  submitAsync(): Promise<{ result: ActorJoinResult; parts: Message[] }> {
    const parts = this._payload.consume();
    return new Promise((resolve, reject) => {
      this._invoke(parts, (result, replyParts) => {
        if (result.result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result.result, 'actor join failed'));
          return;
        }
        resolve({ result, parts: replyParts });
      }, SendFlags.None, this._timeoutMs);
    });
  }

  submit(callback: ActorJoinHandler): boolean {
    const flags = this._callbackMode ? this._flags : SendFlags.None;
    return this._invoke(this._payload.consume(), callback, flags, this._timeoutMs);
  }
}

class ActorJoinReplyOperation implements ActorJoinReplyOp {
  private readonly _invoke: (parts: readonly MessageLike[]) => void;
  private readonly _payload = new OperationPayload();

  constructor(invoke: (parts: readonly MessageLike[]) => void) {
    this._invoke = invoke;
  }

  message(message: MessageLike): ActorJoinReplyOp {
    this._payload.append(message);
    return this;
  }

  submit(): void {
    this._invoke(this._payload.consume());
  }
}

type ReplyOpInvoker = (callback: ReplyHandler, timeoutMs: number) => boolean;

class ReplyHandlerOperation {
  protected readonly _invoke: ReplyOpInvoker;
  protected _timeoutMs = 0;
  protected _submitted = false;

  constructor(invoke: ReplyOpInvoker) {
    this._invoke = invoke;
  }

  protected ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  protected markSubmitted(): void {
    this._submitted = true;
  }

  timeout(timeoutMs: number): this {
    this.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  submitAsync(): Promise<Message[]> {
    this.ensureOpen();
    return new Promise((resolve, reject) => {
      this.markSubmitted();
      this._invoke((result, parts) => {
        if (result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result, 'actor operation failed'));
          return;
        }
        resolve(parts);
      }, this._timeoutMs);
    });
  }

  submit(callback: ReplyHandler): boolean {
    this.ensureOpen();
    this.markSubmitted();
    return this._invoke(callback, this._timeoutMs);
  }
}

class ActorLeaveOperation extends ReplyHandlerOperation implements ActorLeaveOp {}
class ActorDestroyOperation extends ReplyHandlerOperation implements ActorDestroyOp {}
class ActorBindOperation extends ReplyHandlerOperation implements ActorBindOp {}
class ActorUnbindOperation extends ReplyHandlerOperation implements ActorUnbindOp {}

type ActorLookupInvoker = (callback: ActorLookupHandler, timeoutMs: number) => boolean;

class ActorLookupOperation implements ActorLookupOp {
  private readonly _invoke: ActorLookupInvoker;
  private _timeoutMs = 0;
  private _submitted = false;

  constructor(invoke: ActorLookupInvoker) {
    this._invoke = invoke;
  }

  private ensureOpen(): void {
    if (this._submitted) {
      throw new TypeError('operation has already been submitted');
    }
  }

  timeout(timeoutMs: number): ActorLookupOp {
    this.ensureOpen();
    this._timeoutMs = timeoutMs | 0;
    return this;
  }

  submitAsync(): Promise<ActorLookupResult> {
    this.ensureOpen();
    this._submitted = true;
    return new Promise((resolve, reject) => {
      this._invoke((result) => {
        if (result.result !== RequestResult.Ok) {
          reject(requestErrorFromResult(result.result, 'actor lookup failed'));
          return;
        }
        resolve(result);
      }, this._timeoutMs);
    });
  }

  submit(callback: ActorLookupHandler): boolean {
    this.ensureOpen();
    this._submitted = true;
    return this._invoke(callback, this._timeoutMs);
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
  subscribe(result: TopicMessage, flags?: RecvFlags): boolean;
  subscribe(resultOrFlags: TopicMessage | RecvFlags = RecvFlags.None,
            maybeFlags: RecvFlags = RecvFlags.None): TopicMessage | null | boolean {
    const hasResult = resultOrFlags instanceof TopicMessage;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = ((flags | 0) & (RecvFlags.DontWait | 0))
        ? requireNative().spotRecvNoWait(this._native) as any
        : requireNative().spotRecv(this._native, flags | 0) as any;
    } catch (error) {
      throw recvNativeError(error, flags, 'subscribe failed');
    }
    if (!raw) {
      return hasResult ? false : null;
    }
    if (hasResult) {
      adoptTopicMessage(resultOrFlags, raw);
      return true;
    }
    return materializeTopicMessage(raw);
  }
  receiveSubscriptionEvent(result: SubscriptionEvent, flags?: RecvFlags): boolean;
  receiveSubscriptionEvent(resultOrFlags: SubscriptionEvent | RecvFlags = RecvFlags.None,
                           maybeFlags: RecvFlags = RecvFlags.None): SubscriptionEvent | null | boolean {
    const hasResult = resultOrFlags instanceof SubscriptionEvent;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
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
    if (!raw) {
      return hasResult ? false : null;
    }
    const event = SubscriptionEvent.create(
      raw.topic,
      raw.subscribed,
      wrapRoutingId(raw.routingId ?? null)
    );
    if (hasResult) {
      resultOrFlags.adoptFrom(event);
      return true;
    }
    return event;
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
  recvRouted(result: Received, flags?: RecvFlags): boolean;
  recvRouted(flags?: RecvFlags): Received | null;
  recvRouted(resultOrFlags: Received | RecvFlags = RecvFlags.None,
             maybeFlags: RecvFlags = RecvFlags.None): Received | null | boolean {
    const hasResult = resultOrFlags instanceof Received;
    const flags = hasResult ? maybeFlags : resultOrFlags as RecvFlags;
    let raw;
    try {
      raw = requireNative().spotRecvRouted(this._native, flags | 0) as { sourceRid?: Buffer | null; spotRid?: Buffer | null; requestSeq?: bigint | null; parts: MessageSnapshot[] } | null;
    } catch (error) {
      throw recvNativeError(error, flags, 'recvRouted failed');
    }
    if (!raw) return hasResult ? false : null;
    const received = materializeReceived(
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
      },
      (parts, flags) => {
        if (!raw.sourceRid || !raw.spotRid) {
          throw submitErrorFromResult(SubmitResult.InvalidState, 'missing routed send target');
        }
        return this.sendToSpotDirect(
          RoutingId.fromBytes(raw.sourceRid),
          RoutingId.fromBytes(raw.spotRid),
          parts,
          flags
        );
      }
    );
    if (hasResult) {
      resultOrFlags._adoptFrom(received);
      return true;
    }
    return received;
  }
  onRoutedReceive(handler: SpotRoutedHandler): void {
    handlerCall('spot routed handler registration failed', () => {
      requireNative().spotRoutedHandler(this._native, (sourceRid: Buffer | null, spotRid: Buffer | null, requestSeq: bigint, parts: Buffer[]) => {
        const source = wrapRoutingId(sourceRid);
        const spot = wrapRoutingId(spotRid);
        handler(
          Received.create(
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
              : null,
            source && spot
              ? {
                  send: (sendParts, sendFlags) =>
                    this.sendToSpotDirect(source, spot, sendParts, sendFlags)
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
  replyActorJoin(request: ActorJoinRequest, accepted: boolean): ActorJoinReplyOp {
    const spotHandle = this._native;
    const rawInfo = actorJoinInfoToRaw(request.info);
    const acceptedFlag = Boolean(accepted);
    return new ActorJoinReplyOperation((partsInput) => {
      const parts = toMessageParts(partsInput);
      try {
        requireNative().spotActorJoinReply(spotHandle, rawInfo, acceptedFlag, parts);
      } catch (error) {
        throw submitNativeError(error, SendFlags.None, 'actor join reply failed');
      }
    });
  }
  onActorLifecycle(onJoin: ActorLifecycleHandler | null, onLeave: ActorLifecycleHandler | null): void {
    const spotInstance = this;
    handlerCall('spot actor lifecycle handler registration failed', () => {
      requireNative().spotActorLifecycleHandler(
        this._native,
        onJoin
          ? (rawInfo: SpotActorLifecycleInfoRaw) => onJoin(spotInstance, spotActorLifecycleInfoFromRaw(rawInfo))
          : null,
        onLeave
          ? (rawInfo: SpotActorLifecycleInfoRaw) => onLeave(spotInstance, spotActorLifecycleInfoFromRaw(rawInfo))
          : null,
      );
    });
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
  readonly socket: BasePollable | null;
  readonly fd: number | null;
  readonly timer: Timer | null;
  readonly tag: any;
  readonly events: readonly PollEventFlagValue[];
  readonly revents: readonly PollEventFlagValue[];
}

type BasePollable = BaseSocket | Spot;

export class Poller {
  private _native: unknown | null;
  private _nextTagId = 1n;
  private readonly _registrations = new Map<bigint, {
    socket: BasePollable | null;
    fd: number | null;
    timer: Timer | null;
    tag: any;
    events: readonly PollEventFlagValue[];
  }>();
  private readonly _socketIds = new WeakMap<BasePollable, bigint>();
  private readonly _timerIds = new WeakMap<Timer, bigint>();
  private readonly _fdIds = new Map<number, bigint>();
  constructor() { this._native = requireNative().pollerNew(); }
  add(socket: BasePollable, events: readonly PollEventFlagValue[], tag?: any): void;
  add(timer: Timer, tag?: any): void;
  add(item: BasePollable | Timer, eventsOrTag?: readonly PollEventFlagValue[] | any, tag?: any): void {
    if (item instanceof Timer) {
      this.addTimerInternal(item, eventsOrTag);
      return;
    }
    this.addSocketInternal(item, flagsToMask(eventsOrTag as readonly PollEventFlagValue[]), tag, eventsOrTag as readonly PollEventFlagValue[]);
  }
  modify(socket: BasePollable, events: readonly PollEventFlagValue[]): void {
    this.modifySocketInternal(socket, flagsToMask(events));
    const id = this._socketIds.get(socket);
    const current = id ? this._registrations.get(id) : undefined;
    if (id && current) this._registrations.set(id, { ...current, events: Object.freeze(events.slice()) });
  }
  remove(socket: BasePollable): boolean;
  remove(timer: Timer): boolean;
  remove(item: BasePollable | Timer): boolean {
    if (item instanceof Timer) {
      return this.removeTimerInternal(item);
    }
    return this.removeSocketInternal(item);
  }
  private registerSocket(socket: BasePollable, tag: any, events: readonly PollEventFlagValue[]): bigint {
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
  private addSocketInternal(socket: BasePollable, events: number, userData?: any, eventList: readonly PollEventFlagValue[] = maskToFlags(events)): void {
    const id = this.registerSocket(socket, userData, eventList);
    try {
      requireNative().pollerAdd(this._native, socket.nativeHandle(), id, events | 0);
    } catch (error) {
      this._registrations.delete(id);
      this._socketIds.delete(socket);
      throw createError('config', readErrno(), nativeErrorMessage(error, 'poller socket add failed'));
    }
  }
  private modifySocketInternal(socket: BasePollable, events: number): void {
    configCall('poller socket modify failed', () => {
      requireNative().pollerModify(this._native, socket.nativeHandle(), events | 0);
    });
  }
  private removeSocketInternal(socket: BasePollable): boolean {
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
  waitMany(maxEvents: number, timeoutMs: number): PollEvent[] {
    if (!Number.isInteger(maxEvents) || maxEvents <= 0) {
      throw new RangeError('maxEvents must be a positive integer');
    }
    try {
      return (requireNative().pollerWaitMany(this._native, maxEvents | 0, timeoutMs | 0) as RawPollerEvent[])
        .map((raw) => this.eventFromRaw(raw));
    } catch (error) {
      const message = error instanceof Error && error.message ? error.message : String(error);
      if (/Resource temporarily unavailable|temporarily unavailable|would block/i.test(message)) {
        return [];
      }
      throw recvNativeError(error, RecvFlags.None, 'poller waitMany failed');
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
