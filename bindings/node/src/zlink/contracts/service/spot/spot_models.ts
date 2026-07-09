// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../core';
import type { Message } from '../../messaging';
import type { MonitorEvent } from '../../eventing';

export {
  SpotDispatchEvent,
  SpotDispatchSubjectKind,
  type SpotDispatchEventHandler,
  type SpotDispatchInfo,
  type SubscriptionEntry
} from './spot_dispatch_models';
export type {
  SpotDispatchEvent as SpotDispatchEventValue,
  SpotDispatchSubjectKind as SpotDispatchSubjectKindValue
} from './spot_dispatch_models';
export {
  SpotActorLifecycleEventKind,
  type ActorJoinEntrySpotResult,
  type ActorJoinInfo,
  type ActorJoinRequest,
  type ActorJoinResult,
  type ActorLookupResult,
  type ActorPart,
  type ActorRecvInfo,
  type ActorRef,
  type ActorRoute,
  type SpotActorLifecycleEvent,
  type SpotActorLifecycleInfo
} from './actor_models';
export type {
  SpotActorLifecycleEventKind as SpotActorLifecycleEventKindValue
} from './actor_models';
export type {
  SpotNodeActorEntry,
  SpotNodePeerEntry,
  SpotNodePeerFilter,
  SpotNodeSocketEntry,
  SpotNodeSocketFilter,
  SpotNodeSpotEntry,
  SpotNodeStatus,
  SpotNodeSubjectEntry,
  SpotNodeSubjectFilter
} from './spot_node_status_models';

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
