// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../core';
import type { Message, Received } from '../../messaging';
import type { MonitorEvent, Timer } from '../../eventing';
import type { RequestResult } from '../../errors/errors';
import type { RecvFlags } from '../../sockets/socket_constants';

/** The kind of readable event surfaced by a spot dispatch. */
export const SpotDispatchEvent = Object.freeze({
  SubscribeReadable: 1,
  RoutedReadable: 2,
  TimerReadable: 3,
  ChannelReplyReadable: 4,
  ActorReadable: 5,
  ActorJoinReadable: 6,
  ActorLifecycleReadable: 7
} as const);
/** The kind of readable event surfaced by a spot dispatch. */
export type SpotDispatchEvent = typeof SpotDispatchEvent[keyof typeof SpotDispatchEvent];

/** What kind of subject a spot dispatch event concerns. */
export const SpotDispatchSubjectKind = Object.freeze({
  Spot: 1,
  Timer: 2,
  ChannelDealer: 3,
  Actor: 4
} as const);
/** What kind of subject a spot dispatch event concerns. */
export type SpotDispatchSubjectKind = typeof SpotDispatchSubjectKind[keyof typeof SpotDispatchSubjectKind];

/** The pub/sub role of a spot subject. */
export const SpotRole = Object.freeze({ Pub: 1, Sub: 2 } as const);
export type SpotRoleValue = typeof SpotRole[keyof typeof SpotRole];
/** How a spot peer became known to the node. */
export const SpotPeerSource = Object.freeze({ Manual: 1, Discovery: 2, Mixed: 3 } as const);
export type SpotPeerSourceValue = typeof SpotPeerSource[keyof typeof SpotPeerSource];
/** The connection style of a spot peer. */
export const SpotPeerKind = Object.freeze({ SpotMesh: 1, RouterChannel: 2 } as const);
export type SpotPeerKindValue = typeof SpotPeerKind[keyof typeof SpotPeerKind];
/** The connection state of a spot peer. */
export const SpotPeerState = Object.freeze({ Configured: 1, Connecting: 2, Connected: 3 } as const);
export type SpotPeerStateValue = typeof SpotPeerState[keyof typeof SpotPeerState];
/** The kind of a spot (entry or user). */
export const SpotKind = Object.freeze({ Invalid: 0, Entry: 1, User: 2 } as const);
export type SpotKindValue = typeof SpotKind[keyof typeof SpotKind];
/** The overall readiness state of a spot node. */
export const SpotNodeState = Object.freeze({ Idle: 1, Connecting: 2, PartialReady: 3, Ready: 4, Error: 5 } as const);
export type SpotNodeStateValue = typeof SpotNodeState[keyof typeof SpotNodeState];
/** Which messaging patterns a spot node enables. */
export const SpotNodeMode = Object.freeze({ PubSub: 1, Routed: 2, All: 3 } as const);
export type SpotNodeModeValue = typeof SpotNodeMode[keyof typeof SpotNodeMode];
/** Which component owns a spot node socket. */
export const SpotNodeSocketOwner = Object.freeze({ Any: 0, Node: 1, Spot: 2 } as const);
export type SpotNodeSocketOwnerValue = typeof SpotNodeSocketOwner[keyof typeof SpotNodeSocketOwner];

/** Invoked when a socket can accept more sends after back-pressure. */
export type SocketSendReadyHandler = () => void;
/** Invoked for each inbound framed packet with the sender routing id, header, and body; it owns the messages. */
export type StreamPacketHandler = (sourceRid: RoutingId, header: Message, body: Message) => void;
/** Invoked for each socket monitor event. */
export type SocketMonitorHandler = (event: MonitorEvent) => void;
/** Invoked when a spot can accept more sends after back-pressure. */
export type SpotSendReadyHandler = () => void;
/** A reference to an actor: the node hosting it, its id, and its generation. */
export interface ActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}
/** The resolved route to an actor: which spot it currently lives on. */
export interface ActorRoute {
  readonly actor: ActorRef;
  readonly currentSpotRid: RoutingId;
  readonly currentSpotKind: SpotKindValue;
}
/** Metadata about a message received for an actor. */
export interface ActorRecvInfo {
  readonly actor: ActorRef;
  readonly sourceNodeRid: RoutingId;
  readonly sourceSessionRid: RoutingId;
  readonly flags: number;
}
/** Details of an actor-join request: the actors and spots on each side. */
export interface ActorJoinInfo {
  readonly sourceActor: ActorRef;
  readonly targetActor: ActorRef;
  readonly sourceNodeRid: RoutingId;
  readonly sourceSpotRid?: RoutingId;
  readonly targetNodeRid: RoutingId;
  readonly targetSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}
/** One part of a message received for an actor, with `more` indicating further parts. */
export interface ActorPart {
  readonly info: ActorRecvInfo;
  readonly message: Message;
  readonly more: boolean;
}
/** A pending request from an actor to join a spot, awaiting a reply. */
export interface ActorJoinRequest {
  readonly info: ActorJoinInfo;
  readonly message: Message;
}
/** The outcome of an actor join. */
export interface ActorJoinResult {
  readonly result: RequestResult;
  readonly joinResultCode: number;
  readonly actor: ActorRef;
  readonly joinedSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}
/** The outcome of an actor join routed through an entry spot. */
export interface ActorJoinEntrySpotResult {
  readonly result: RequestResult;
  readonly joinResultCode: number;
  readonly actor: ActorRef;
  readonly targetNodeRid: RoutingId;
  readonly joinedSpotRid: RoutingId;
  readonly joinEpoch: bigint;
  readonly flags: number;
}
/** The outcome of an actor lookup. */
export interface ActorLookupResult {
  readonly result: RequestResult;
  readonly actor: ActorRef;
  readonly flags: number;
}
/** Details of an actor lifecycle change, before and after. */
export interface SpotActorLifecycleInfo {
  readonly previousActor: ActorRef;
  readonly currentActor: ActorRef;
  readonly previousSpotRid: RoutingId | null;
  readonly currentSpotRid: RoutingId | null;
  readonly joinEpoch: bigint;
  readonly flags: number;
}
/** Whether an actor joined or left a spot. */
export const SpotActorLifecycleEventKind = Object.freeze({
  Joined: 1,
  Left: 2
} as const);
/** Whether an actor joined or left a spot. */
export type SpotActorLifecycleEventKind =
  typeof SpotActorLifecycleEventKind[keyof typeof SpotActorLifecycleEventKind];
/** An actor join/leave lifecycle event observed on a spot. */
export interface SpotActorLifecycleEvent {
  readonly kind: SpotActorLifecycleEventKind;
  readonly info: SpotActorLifecycleInfo;
  /** First create request part, or an empty message when no request was supplied. */
  readonly message: Message;
  /** All create request parts in the order supplied to actor creation. */
  readonly parts: readonly Message[];
}
/** One spot hosted on a spot node and its actor counts. */
export interface SpotNodeSpotEntry {
  readonly spotRid: RoutingId;
  readonly spotKind: SpotKindValue;
  readonly dispatchHandlerAttached: boolean;
  readonly joinedActorCount: number;
  readonly pendingActorJoinCount: number;
  readonly routeSynced: boolean;
  readonly lastChangedMs: bigint;
}
/** One actor hosted on a spot node and its current placement. */
export interface SpotNodeActorEntry {
  readonly actor: ActorRef;
  readonly currentSpotRid: RoutingId;
  readonly currentSpotKind: SpotKindValue;
  readonly routeSynced: boolean;
  readonly pendingMessageCount: number;
  readonly lastChangedMs: bigint;
}
/** The event and context passed to an on-dispatch-event callback. */
export interface SpotDispatchInfo {
  readonly event: SpotDispatchEvent;
  readonly subjectKind: SpotDispatchSubjectKind;
  readonly timer: Timer | null;
  readonly actorRef: ActorRef | null;
  readonly routed: Received | null;
  recvActorPart(flags?: RecvFlags): ActorPart | null;
}
/** Invoked for each spot dispatch event. */
export type SpotDispatchEventHandler = (info: SpotDispatchInfo) => void;
