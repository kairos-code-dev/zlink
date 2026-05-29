// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../core';
import type { Message } from '../../messaging';
import type { MonitorEvent, Timer } from '../../eventing';
import type { RequestResult } from '../../errors/errors';
import type { RecvFlags } from '../../sockets/socket_constants';

export const SpotDispatchEvent = Object.freeze({
  SubscribeReadable: 1,
  RoutedReadable: 2,
  TimerReadable: 3,
  ChannelReplyReadable: 4,
  ActorReadable: 5,
  ActorJoinReadable: 6,
  ActorLifecycleReadable: 7
} as const);
export type SpotDispatchEvent = typeof SpotDispatchEvent[keyof typeof SpotDispatchEvent];

export const SpotDispatchSubjectKind = Object.freeze({
  Spot: 1,
  Timer: 2,
  ChannelDealer: 3,
  Actor: 4
} as const);
export type SpotDispatchSubjectKind = typeof SpotDispatchSubjectKind[keyof typeof SpotDispatchSubjectKind];

export const SpotRole = Object.freeze({ Pub: 1, Sub: 2 } as const);
export type SpotRoleValue = typeof SpotRole[keyof typeof SpotRole];
export const SpotPeerSource = Object.freeze({ Manual: 1, Discovery: 2, Mixed: 3 } as const);
export type SpotPeerSourceValue = typeof SpotPeerSource[keyof typeof SpotPeerSource];
export const SpotPeerKind = Object.freeze({ SpotMesh: 1, RouterChannel: 2 } as const);
export type SpotPeerKindValue = typeof SpotPeerKind[keyof typeof SpotPeerKind];
export const SpotPeerState = Object.freeze({ Configured: 1, Connecting: 2, Connected: 3 } as const);
export type SpotPeerStateValue = typeof SpotPeerState[keyof typeof SpotPeerState];
export const SpotKind = Object.freeze({ Invalid: 0, Entry: 1, User: 2 } as const);
export type SpotKindValue = typeof SpotKind[keyof typeof SpotKind];
export const SpotNodeState = Object.freeze({ Idle: 1, Connecting: 2, PartialReady: 3, Ready: 4, Error: 5 } as const);
export type SpotNodeStateValue = typeof SpotNodeState[keyof typeof SpotNodeState];
export const SpotNodeMode = Object.freeze({ PubSub: 1, Routed: 2, All: 3 } as const);
export type SpotNodeModeValue = typeof SpotNodeMode[keyof typeof SpotNodeMode];
export const SpotNodeSocketOwner = Object.freeze({ Any: 0, Node: 1, Spot: 2 } as const);
export type SpotNodeSocketOwnerValue = typeof SpotNodeSocketOwner[keyof typeof SpotNodeSocketOwner];

export type SocketSendReadyHandler = () => void;
export type StreamPacketHandler = (sourceRid: RoutingId, header: Message, body: Message) => void;
export type SocketMonitorHandler = (event: MonitorEvent) => void;
export type SpotSendReadyHandler = () => void;
export interface ActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}
export interface ActorRoute {
  readonly actor: ActorRef;
  readonly currentSpotRid: RoutingId;
  readonly currentSpotKind: SpotKindValue;
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
  readonly joinResultCode: number;
  readonly actor: ActorRef;
  readonly joinedSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}
export interface ActorJoinEntrySpotResult {
  readonly result: RequestResult;
  readonly actor: ActorRef;
  readonly targetNodeRid: RoutingId;
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
export const SpotActorLifecycleEventKind = Object.freeze({
  Joined: 1,
  Left: 2
} as const);
export type SpotActorLifecycleEventKind =
  typeof SpotActorLifecycleEventKind[keyof typeof SpotActorLifecycleEventKind];
export interface SpotActorLifecycleEvent {
  readonly kind: SpotActorLifecycleEventKind;
  readonly info: SpotActorLifecycleInfo;
}
export interface SpotNodeSpotEntry {
  readonly spotRid: RoutingId;
  readonly spotKind: SpotKindValue;
  readonly dispatchHandlerAttached: boolean;
  readonly joinedActorCount: number;
  readonly pendingActorJoinCount: number;
  readonly routeSynced: boolean;
  readonly lastChangedMs: bigint;
}
export interface SpotNodeActorEntry {
  readonly actor: ActorRef;
  readonly currentSpotRid: RoutingId;
  readonly currentSpotKind: SpotKindValue;
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
export function wrapRoutingId(routingId: Buffer | Uint8Array | null | undefined): RoutingId | null {
  if (!routingId || routingId.length === 0) {
    return null;
  }
  return RoutingId.from(routingId);
}
