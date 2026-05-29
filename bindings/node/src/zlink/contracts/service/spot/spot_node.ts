// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../../core';
import type { DealerSocket, PubSocket } from '../../sockets';
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

export interface SpotNode {
  setPubBind(endpoint: string): void;
  setRouterBind(endpoint: string): void;
  connectPeer(endpoint: string): void;
  disconnectPeer(endpoint: string): void;
  disconnectPeerRid(targetNodeRid: RoutingId): void;
  connectRouterChannelPeer(channelName: string, endpoint: string): void;
  disconnectRouterChannelPeer(channelName: string, endpoint: string): void;
  disconnectRouterChannelPeerRid(channelName: string, peerRid: RoutingId): void;
  attachChannelDealer(discovery: Discovery, dealer: DealerSocket): void;
  attachChannelDealerManual(channelName: string, dealer: DealerSocket): void;
  attachPubIngress(pub: PubSocket): void;
  attachDiscovery(discovery: Discovery): void;
  attachSpotRouteChannelDiscovery(channelName: string, discovery: Discovery): void;
  setRoutingId(routingId: RoutingId): void;
  readonly routingId: RoutingId;
  setTlsServer(cert: string, key: string, requireClientCert?: boolean): void;
  setTlsClient(ca: string, hostname: string, trustSystem?: boolean): void;
  routerHwmProfile: number;
  routerHwm: number;
  pubsubHwmProfile: number;
  pubsubHwm: number;
  dispatchWorkersMin: number;
  dispatchWorkersMax: number;
  createSpot(): Spot;
  entrySpot(): Spot;
  spotLookup(spotRid: RoutingId): Spot | null;
  getOrCreateSpot(spotRid: RoutingId): { spot: Spot; created: boolean };
  createActor(actorId: string): Actor;
  actorLookup(actorId: string): ActorRef;
  remoteActorGetRef(targetNodeRid: RoutingId, actorId: string): ActorLookupOperation;
  destroyActor(actor: ActorRef): ActorDestroyOperation;
  joinActor(actor: ActorRef, targetNodeRid: RoutingId, targetSpotRid: RoutingId): ActorJoinOperation;
  joinActorEntrySpot(actor: ActorRef, targetNodeRid: RoutingId): ActorJoinEntrySpotOperation;
  leaveActor(actor: ActorRef, targetSpotRid: RoutingId): ActorLeaveOperation;
  sendActorBoundSession(actor: ActorRef): SendOperation;
  status(): SpotNodeStatus;
  peers(filter?: SpotNodePeerFilter): SpotNodePeerEntry[];
  subjects(filter?: SpotNodeSubjectFilter): SpotNodeSubjectEntry[];
  internalSockets(filter?: SpotNodeSocketFilter): SpotNodeSocketEntry[];
  spots(): SpotNodeSpotEntry[];
  actors(): SpotNodeActorEntry[];
  close(): void;
}

export type { SpotNodeModeValue };
