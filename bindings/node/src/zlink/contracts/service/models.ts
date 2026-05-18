// SPDX-License-Identifier: MPL-2.0

import {
  Message,
  Received,
  RoutingId,
  TopicMessage,
  SubscriptionEvent,
  type MessageLike,
} from '../messaging/message';
import { SendFlags, RecvFlags, type PollEventFlagValue } from '../enums/socket_constants';
import { RequestResult } from '../errors/errors';
import type { Spot, Timer } from '../../runtime/core/canonical';

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
  BLOCKY: 10,
  AUTO_HWM_ENABLE: 12,
  AUTO_HWM_RECALC_DEBOUNCE_MS: 14,
  AUTO_HWM_PROFILE: 17,
  AUTO_HWM_MSG_UNIT_BYTES: 18
} as const);

export const AutoHwmProfile = Object.freeze({
  Compact: 0,
  LowLatency: 1,
  Balanced: 2,
  Throughput: 3
} as const);
export type AutoHwmProfileValue = typeof AutoHwmProfile[keyof typeof AutoHwmProfile];

export const SpotNodeOption = Object.freeze({
  ROUTER_HWM_PROFILE: 0x360E,
  ROUTER_HWM: 0x360F,
  PUBSUB_HWM_PROFILE: 0x3610,
  PUBSUB_HWM: 0x3611,
  DISPATCH_WORKERS_MIN: 0x3612,
  DISPATCH_WORKERS_MAX: 0x3613
} as const);

export const SpotOption = Object.freeze({
  REQUEST_TIMEOUT_MS: 0x3701
} as const);

export const MonitorSourceKind = Object.freeze({ Socket: 1, SpotPub: 3, SpotSub: 4 } as const);
export type MonitorSourceKindValue = typeof MonitorSourceKind[keyof typeof MonitorSourceKind];
export const MonitorState = Object.freeze({
  READY: 1 << 0, BOUND_READY: 1 << 1, CLOSED: 1 << 3
} as const);
export const MonitorSnapshotDetail = Object.freeze({
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

export interface MonitorSnapshotRaw {
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

export interface MonitorEventValueRaw {
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

export type SocketSendReadyHandler = () => void;
export type StreamPacketHandler = (sourceRid: RoutingId, header: Message, body: Message) => void;
export type SocketMonitorHandler = (event: MonitorEvent) => void;
export type SpotSendReadyHandler = () => void;
export type SpotRoutedHandler = (message: Received) => void;
export interface ActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
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
export interface ActorJoinResult {
  readonly result: RequestResult;
  readonly actor: ActorRef;
  readonly joinedSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}
export interface ActorLookupResult {
  readonly result: RequestResult;
  readonly actor: ActorRef;
  readonly flags: number;
}
export interface SpotActorLifecycleInfo {
  readonly previousActor: ActorRef;
  readonly currentActor: ActorRef;
  readonly previousSpotRid: RoutingId | null;
  readonly currentSpotRid: RoutingId | null;
  readonly joinEpoch: bigint;
  readonly flags: number;
}
export type ActorJoinHandler = (result: ActorJoinResult, parts: Message[]) => void;
export type ActorLookupHandler = (result: ActorLookupResult) => void;
export type ActorLifecycleHandler = (spot: Spot, info: SpotActorLifecycleInfo) => void;
export type ReplyHandler = (result: RequestResult, parts: Message[]) => void;
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

export interface ActorJoinOp {
  message(message: MessageLike): ActorJoinSubmitOp;
}

export interface ActorJoinSubmitOp {
  message(message: MessageLike): ActorJoinSubmitOp;
  timeout(timeoutMs: number): ActorJoinSubmitOp;
  flags(flags: SendFlags): ActorJoinCallbackSubmitOp;
  submitAsync(): Promise<{ result: ActorJoinResult; parts: Message[] }>;
  submit(callback: ActorJoinHandler): boolean;
}

export interface ActorJoinCallbackSubmitOp {
  message(message: MessageLike): ActorJoinCallbackSubmitOp;
  timeout(timeoutMs: number): ActorJoinCallbackSubmitOp;
  flags(flags: SendFlags): ActorJoinCallbackSubmitOp;
  submit(callback: ActorJoinHandler): boolean;
}

export interface ActorJoinReplyOp {
  message(message: MessageLike): ActorJoinReplyOp;
  submit(): void;
}

export interface ActorLeaveOp {
  timeout(timeoutMs: number): ActorLeaveOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorDestroyOp {
  timeout(timeoutMs: number): ActorDestroyOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorLookupOp {
  timeout(timeoutMs: number): ActorLookupOp;
  submitAsync(): Promise<ActorLookupResult>;
  submit(callback: ActorLookupHandler): boolean;
}

export interface ActorBindOp {
  timeout(timeoutMs: number): ActorBindOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export interface ActorUnbindOp {
  timeout(timeoutMs: number): ActorUnbindOp;
  submitAsync(): Promise<Message[]>;
  submit(callback: ReplyHandler): boolean;
}

export function wrapRoutingId(routingId: Buffer | Uint8Array | null | undefined): RoutingId | null {
  if (!routingId || routingId.length === 0) {
    return null;
  }
  return RoutingId.fromBytes(routingId);
}
