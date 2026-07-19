// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../core';
import type { MessageLike } from '../messaging';
import type { RequestResult, SubmitResult } from '../errors';
import type { StreamSocket } from '../sockets';
import type {
  ActorLocation,
  ActorRef,
  DrainReadyResult,
  MeshOperationId,
  ReadyBatch,
  ReceiveBatch
} from './dispatch';
import type { ActorTransferPrepare, ActorTransferToken, PrepareActorTransferResult } from './transfer';
import type { MessagingOptions, RequestOptions } from './shared';
import type { Spot } from './spot';
import type { Publisher } from './publisher';
import type { StreamSessionService } from './stream_session';
import type { RecvFlags } from '../sockets';

/**
 * The overall readiness state of a mesh node. Maps one-to-one to the Core wire
 * enum `zlink_mesh_node_state_t`; the addon passes these values through
 * unchanged.
 */
export const MeshNodeState = Object.freeze({
  Created: 1,
  Started: 2,
  PartialReady: 3,
  Ready: 4,
  Draining: 5,
  Stopped: 6,
  Error: 7
} as const);
export type MeshNodeStateValue = typeof MeshNodeState[keyof typeof MeshNodeState];

export const MeshMonitorEventKind = Object.freeze({
  StateChanged: 1, PeerConnecting: 2, PeerAdmitted: 3, PeerDraining: 4,
  PeerClosed: 5, PeerRejected: 6, ChannelChanged: 7,
  MessageSubmitted: 8, MulticastCommitted: 9, MulticastDropped: 10,
  Backpressured: 11, OperationCompleted: 12, ProtocolError: 13,
  ClaimRevoked: 14
} as const);

export const MeshMonitorEventMask = Object.freeze({
  StateChanged: 1n << 0n, PeerConnecting: 1n << 1n,
  PeerAdmitted: 1n << 2n, PeerDraining: 1n << 3n,
  PeerClosed: 1n << 4n, PeerRejected: 1n << 5n,
  ChannelChanged: 1n << 6n, MessageSubmitted: 1n << 7n,
  MulticastCommitted: 1n << 8n, MulticastDropped: 1n << 9n,
  Backpressured: 1n << 10n, OperationCompleted: 1n << 11n,
  ProtocolError: 1n << 12n, ClaimRevoked: 1n << 13n,
  All: (1n << 14n) - 1n
} as const);

export interface MeshMonitorEvent {
  readonly kind: number;
  readonly timestampMs: bigint;
  readonly meshLifecycleGeneration: bigint;
  readonly meshDescriptorRevision: bigint;
  readonly meshState: number;
  readonly peerRid: RoutingId;
  readonly peerLifecycleGeneration: bigint;
  readonly peerDescriptorRevision: bigint;
  readonly ownerKind: number;
  readonly spotRid: RoutingId;
  readonly actor: ActorRef;
  readonly channelName: string;
  readonly operationId: MeshOperationId;
  readonly snapshotRemoteTargetCount: number;
  readonly admittedRemoteTargetCount: number;
  readonly droppedRemoteTargetCount: number;
  readonly unreachableRemoteTargetCount: number;
  readonly snapshotLocalSpotCount: number;
  readonly admittedLocalSpotCount: number;
  readonly droppedLocalSpotCount: number;
  readonly resultCode: number;
  readonly failureErrno: number;
}

export interface MeshMonitorStatus {
  readonly state: number;
  readonly peerAdmitted: bigint;
  readonly peerRejected: bigint;
  readonly submittedMessages: bigint;
  readonly completedOperations: bigint;
  readonly backpressuredSubmits: bigint;
  readonly multicastMessages: bigint;
  readonly multicastDroppedTargets: bigint;
  readonly activeClaims: bigint;
  readonly pendingApplicationMessages: bigint;
  readonly pendingInfrastructureMessages: bigint;
  readonly pendingBytes: bigint;
}

export interface MeshNodeMonitor {
  recv(flags?: RecvFlags): MeshMonitorEvent | null;
  status(): MeshMonitorStatus;
  close(): void;
}

/** A point-in-time snapshot of a mesh node's state. */
export interface MeshNodeStatus {
  readonly state: number;
  readonly routingId: RoutingId;
  readonly meshName: string;
  readonly localEndpoint: string;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly channelCount: number;
  readonly configuredPeerCount: number;
  readonly admittedPeerCount: number;
  readonly drainingPeerCount: number;
  readonly pendingApplicationMessages: bigint;
  readonly pendingInfrastructureMessages: bigint;
  readonly pendingBytes: bigint;
  readonly multicastSubmitted: bigint;
  readonly multicastDroppedTargets: bigint;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

/** One peer known to the mesh node. */
export interface MeshPeerEntry {
  readonly connectionIntentId: bigint;
  readonly source: number;
  readonly state: number;
  readonly routingId: RoutingId;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly endpoint: string;
  readonly channelCount: number;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

/** The channel names and weights advertised by a peer. */
export interface PeerChannels {
  readonly names: string[];
  readonly weights: number[];
}

/** Options accepted by {@link MeshNode.connectPeer}. */
export interface ConnectPeerOptions {
  readonly endpoint: string;
  readonly expectedRid?: RoutingId;
}

/** The result of {@link MeshNode.getOrCreateSpot}. */
export interface GetOrCreateSpotResult {
  readonly spot: Spot;
  readonly created: boolean;
}

/**
 * A mesh node: the local participant of a RouteMesh. It owns spots, publishers,
 * and actors; connects to peers; and drives pull-based dispatch.
 */
export interface MeshNode {
  // --- Lifecycle ---------------------------------------------------------
  /** Set the endpoint the node binds when it starts. */
  setBind(endpoint: string): void;
  /** Start the node. */
  start(): void;
  /** Shut the node down, waiting up to `timeoutMs`; returns the outcome. */
  shutdown(timeoutMs: number): RequestResult;
  /** Release native resources held by the node. */
  close(): void;
  /** Advertise a channel this node participates in. */
  addChannelName(name: string): void;
  /** Set the routing weight for a channel. */
  setChannelWeight(name: string, weight: number): void;

  // --- Peers -------------------------------------------------------------
  /** Begin connecting to a peer; returns the connection intent id. */
  connectPeer(options: ConnectPeerOptions): bigint;
  /** Cancel a pending connection intent. */
  removePeerConnection(intentId: bigint): void;
  /** Disconnect an admitted peer at a known lifecycle generation. */
  disconnectPeer(peerRid: RoutingId, lifecycleGeneration: bigint): void;

  // --- Node / channel messaging -----------------------------------------
  /** Send parts to a specific node; returns the submit outcome. */
  sendToNode(targetRid: RoutingId, parts: MessageLike | readonly MessageLike[], options?: MessagingOptions): SubmitResult;
  /** Request to a specific node; completion arrives through pull dispatch. */
  requestToNode(targetRid: RoutingId, parts: MessageLike | readonly MessageLike[], options?: RequestOptions): MeshOperationId;
  /** Send parts across a channel; returns the submit outcome. */
  sendToChannel(channelName: string, parts: MessageLike | readonly MessageLike[], options?: MessagingOptions): SubmitResult;
  /** Request across a channel; completion arrives through pull dispatch. */
  requestToChannel(channelName: string, parts: MessageLike | readonly MessageLike[], options?: RequestOptions): MeshOperationId;

  // --- Publisher ---------------------------------------------------------
  /** Create a publisher owned by this node. */
  createPublisher(): Publisher;

  // --- Options / introspection ------------------------------------------
  /** Apply a node option. */
  setOption(option: number, value: Buffer): void;
  /** Read a node option. */
  getOption(option: number): Buffer;
  /** Return a fresh status snapshot. */
  status(): MeshNodeStatus;
  /** Return the current peer table. */
  peers(): MeshPeerEntry[];
  /** Return the channel names and weights advertised by a peer. */
  peerChannels(peerRid: RoutingId, generation: bigint): PeerChannels;
  /** Open the push monitor for this node. */
  openMonitor(events?: bigint): MeshNodeMonitor;

  // --- Spots -------------------------------------------------------------
  /** Create a new user spot. */
  createSpot(): Spot;
  /** Return this node's entry spot. */
  entrySpot(): Spot;
  /** Look up a local spot by routing id, or null when absent. */
  spotLookup(rid: RoutingId): Spot | null;
  /** Look up a spot by routing id, creating it when absent. */
  getOrCreateSpot(rid: RoutingId): GetOrCreateSpotResult;

  // --- Actors ------------------------------------------------------------
  /** Create a local actor; returns its reference. */
  createActor(
    actorId: string,
    creationParts?: MessageLike | readonly MessageLike[],
    options?: { flags?: number; timeoutMs?: number }
  ): ActorRef;
  /** Look up a local actor's location. */
  actorLookup(actorId: string): ActorLocation;
  /** Look up an actor on a remote node; completion arrives through pull dispatch. */
  lookupRemoteActor(targetNodeRid: RoutingId, actorId: string, timeoutMs?: number): MeshOperationId;
  /** Destroy an actor; completion arrives through pull dispatch. */
  destroyActor(actor: ActorRef, timeoutMs?: number): MeshOperationId;
  /** Join an actor to a remote spot; completion arrives through pull dispatch. */
  joinActorSpot(
    actor: ActorRef,
    targetNodeRid: RoutingId,
    targetSpotRid: RoutingId,
    targetSpotGeneration: bigint,
    creationParts?: MessageLike | readonly MessageLike[],
    timeoutMs?: number
  ): MeshOperationId;
  /** Join an actor to a remote node's entry spot; completion arrives through pull dispatch. */
  joinActorEntrySpot(
    actor: ActorRef,
    targetNodeRid: RoutingId,
    creationParts?: MessageLike | readonly MessageLike[],
    timeoutMs?: number
  ): MeshOperationId;
  /** Leave an actor from its spot; completion arrives through pull dispatch. */
  leaveActor(actor: ActorRef, expectedMembershipEpoch: bigint, timeoutMs?: number): MeshOperationId;

  /** Send parts to an actor; returns the submit outcome. */
  sendToActor(actor: ActorRef, parts: MessageLike | readonly MessageLike[], options?: MessagingOptions): SubmitResult;
  /** Request to an actor; completion arrives through pull dispatch. */
  requestToActor(actor: ActorRef, parts: MessageLike | readonly MessageLike[], options?: RequestOptions): MeshOperationId;
  /** Send from one actor to another; returns the submit outcome. */
  actorSendToActor(
    source: ActorRef,
    target: ActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: MessagingOptions
  ): SubmitResult;
  /** Request from one actor to another; completion arrives through pull dispatch. */
  actorRequestToActor(
    source: ActorRef,
    target: ActorRef,
    parts: MessageLike | readonly MessageLike[],
    options?: RequestOptions
  ): MeshOperationId;
  /** Send parts over an actor's bound session; returns the submit outcome. */
  sendActorBoundSession(actor: ActorRef, parts: MessageLike | readonly MessageLike[], flags?: number): SubmitResult;
  /** Close an actor's bound session; completion arrives through pull dispatch. */
  closeActorBoundSession(actor: ActorRef, expectedBindingGeneration: bigint, timeoutMs?: number): MeshOperationId;

  // --- Actor transfer fence ---------------------------------------------
  /** Prepare an actor transfer; returns the fence token and its result detail. */
  prepareActorTransfer(prepare: ActorTransferPrepare, timeoutMs?: number): PrepareActorTransferResult;
  /** Commit a prepared actor transfer at the new membership epoch. */
  commitActorTransfer(token: ActorTransferToken, newMembershipEpoch: bigint): void;
  /** Activate a committed actor transfer on the target. */
  activateActorTransfer(token: ActorTransferToken): void;
  /** Abort a prepared actor transfer, releasing the fence. */
  abortActorTransfer(token: ActorTransferToken): void;

  // --- Pull dispatch -----------------------------------------------------
  /** Install the handler invoked with the ready-domain mask; return the mask to drain. */
  setReadyHandler(handler: (readyDomains: number) => number): void;
  /** Create a reusable ready batch with capacity for `capacity` records. */
  createReadyBatch(capacity: number): ReadyBatch;
  /** Create a reusable receive batch with the given message/part/byte capacities. */
  createReceiveBatch(messageCapacity: number, partCapacity: number, byteCapacity: number): ReceiveBatch;
  /** Drain the ready index for `domains` into `batch`. */
  drainReady(domains: number, batch: ReadyBatch, flags?: number): DrainReadyResult;

  // --- Stream session service -------------------------------------------
  /** Create a stream-session service backed by `stream`. */
  createStreamSessionService(stream: StreamSocket): StreamSessionService;
}
