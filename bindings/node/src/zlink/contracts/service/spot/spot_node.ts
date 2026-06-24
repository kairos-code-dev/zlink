// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../../core';
import type { MessageLike } from '../../messaging';
import type { Discovery } from '../discovery/discovery';
import type {
  ActorDestroyOperation,
  ActorJoinEntrySpotOperation,
  ActorJoinOperation,
  ActorLeaveOperation,
  ActorLookupOperation,
  ActorRef,
  SendOperation,
  SpotNodeActorEntry,
  SpotNodeModeValue,
  SpotNodePeerEntry,
  SpotNodePeerFilter,
  SpotNodeSocketEntry,
  SpotNodeSocketFilter,
  SpotNodeSpotEntry,
  SpotNodeStatus,
  SpotNodeSubjectEntry,
  SpotNodeSubjectFilter,
} from '../index';
import type { Actor } from './actor';
import type { Spot } from './spot';
import type { SpotNodePublisher, SpotRouteBridge } from './spot_route_bridge';

/** A spot node: hosts spots and actors, tunes their sockets, and exposes the node's peers, subjects, and topology. */
export interface SpotNode {
  /** Set the publish endpoint the node binds to. */
  setPubBind(endpoint: string): void;
  /** Set the router endpoint the node binds to. */
  setRouterBind(endpoint: string): void;
  /** Connect to a peer node at `endpoint`. */
  connectPeer(endpoint: string): void;
  /** Connect to a peer node identified by `targetNodeRid` at `endpoint`. */
  connectPeerRid(targetNodeRid: RoutingId, endpoint: string): void;
  /** Disconnect the peer previously connected at `endpoint`. */
  disconnectPeer(endpoint: string): void;
  /** Disconnect the peer identified by `targetNodeRid`. */
  disconnectPeerRid(targetNodeRid: RoutingId): void;
  /** Create a bridge for caller-owned channel sockets. */
  createRouteBridge(): SpotRouteBridge;
  /** Create a publisher handle for the local SPOT topic plane. */
  createPublisher(): SpotNodePublisher;
  /** Attach a discovery service so the node auto-connects discovered peers. */
  attachDiscovery(discovery: Discovery): void;
  /** Set the node's routing id. */
  setRoutingId(routingId: RoutingId): void;
  /** The node's routing id. */
  readonly routingId: RoutingId;
  /** Configure the node as a TLS server; apply before binding. */
  setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
  /** Configure TLS for the node's outbound connections. */
  setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
  /** The auto-HWM profile for the node's router sockets. */
  routerHwmProfile: number;
  /** The high-water mark applied to the node's router sockets. */
  routerHwm: number;
  /** The auto-HWM profile for the node's pub/sub sockets. */
  pubsubHwmProfile: number;
  /** The high-water mark applied to the node's pub/sub sockets. */
  pubsubHwm: number;
  /** The minimum number of dispatch worker threads. */
  dispatchWorkersMin: number;
  /** The maximum number of dispatch worker threads. */
  dispatchWorkersMax: number;
  /** Create a new user spot on the node. The caller owns it. */
  createSpot(): Spot;
  /** Return the node's entry spot. */
  entrySpot(): Spot;
  /** Look up a spot by routing id, or null when absent. */
  spotLookup(spotRid: RoutingId): Spot | null;
  /** Return the spot for `spotRid`, creating it if absent; `created` is true when newly created. */
  getOrCreateSpot(spotRid: RoutingId): { spot: Spot; created: boolean };
  /** Create a local actor with id `actorId`. Optional request parts are delivered to the actor-created lifecycle event. */
  createActor(actorId: string, request?: MessageLike | readonly MessageLike[]): Actor;
  /** Look up a local actor by id, returning its reference. */
  actorLookup(actorId: string): ActorRef;
  /** Begin looking up a remote actor's reference; submit the returned operation. */
  remoteActorGetRef(targetNodeRid: RoutingId, actorId: string): ActorLookupOperation;
  /** Begin destroying `actor`; submit the returned operation to apply it. */
  destroyActor(actor: ActorRef): ActorDestroyOperation;
  /** Begin joining `actor` to a spot on another node; submit the returned operation. */
  joinActor(actor: ActorRef, targetNodeRid: RoutingId, targetSpotRid: RoutingId): ActorJoinOperation;
  /** Begin joining `actor` to a node's entry spot with a request message. */
  joinActorEntrySpot(actor: ActorRef, targetNodeRid: RoutingId, request: MessageLike): ActorJoinEntrySpotOperation;
  /** Begin removing `actor` from `targetSpotRid`; submit the returned operation. */
  leaveActor(actor: ActorRef, targetSpotRid: RoutingId): ActorLeaveOperation;
  /** Begin a send to `actor`'s bound session; parts are consumed on a successful submit. */
  sendActorBoundSession(actor: ActorRef): SendOperation;
  /** Bind a remote actor to a session owned by another node. */
  bindRemoteActorSession(actor: ActorRef, sourceNodeRid: RoutingId, sourceSessionRid: RoutingId): void;
  /** Return a snapshot of the node's status. */
  status(): SpotNodeStatus;
  /** Return the node's peers, optionally filtered. */
  peers(filter?: SpotNodePeerFilter): SpotNodePeerEntry[];
  /** Return the node's subjects, optionally filtered. */
  subjects(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[];
  /** Return the node's internal sockets, optionally filtered. */
  internalSockets(filter?: SpotNodeSocketFilter): SpotNodeSocketEntry[];
  /** Return the spots hosted on the node. */
  spots(): SpotNodeSpotEntry[];
  /** Return the actors hosted on the node. */
  actors(): SpotNodeActorEntry[];
  /** Close the node and release its resources. */
  close(): void;
}

export type { SpotNodeModeValue };
