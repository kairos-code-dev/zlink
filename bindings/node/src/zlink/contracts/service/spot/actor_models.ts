// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../core';
import type { Message } from '../../messaging';
import type { RequestResult } from '../../errors/errors';
import type { SpotKindValue } from './spot_models';

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
  readonly requestId: bigint;
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

/** Whether an actor joined, left, or disconnected from a spot. */
export const SpotActorLifecycleEventKind = Object.freeze({
  Joined: 1,
  Left: 2,
  Disconnected: 3
} as const);

/** Whether an actor joined, left, or disconnected from a spot. */
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
