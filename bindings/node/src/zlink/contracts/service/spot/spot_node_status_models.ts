// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../core';
import type { MonitorStatus } from '../../eventing';
import type { SocketTypeValue } from '../../sockets/socket_constants';
import type { ActorRef } from './actor_models';
import type {
  SpotKindValue,
  SpotNodeSocketOwnerValue,
  SpotNodeStateValue,
  SpotPeerKindValue,
  SpotPeerSourceValue,
  SpotPeerStateValue,
  SpotRoleValue
} from './spot_models';

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

/** A snapshot of a spot node's status and peer/subject counts. */
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

/** One peer of a spot node and its connection details. */
export interface SpotNodePeerEntry {
  readonly channelName: string;
  readonly localEndpoint: string;
  readonly peerEndpoint: string;
  readonly source: SpotPeerSourceValue;
  readonly kind: SpotPeerKindValue;
  readonly state: SpotPeerStateValue;
  readonly weight: number;
  readonly connectedSinceMs: bigint;
  readonly lastChangedMs: bigint;
}

/** One subject served by a spot node. */
export interface SpotNodeSubjectEntry {
  readonly role: SpotRoleValue;
  readonly subject: string;
  readonly subjectKind: number;
  readonly readyPeerCount: number;
  readonly activePeerCount: number;
  readonly lastChangedMs: bigint;
}

/** Filter for a spot node socket query; omitted fields match anything. */
export interface SpotNodeSocketFilter {
  readonly owner?: SpotNodeSocketOwnerValue;
  readonly socketType?: SocketTypeValue;
  readonly socketName?: string;
}

/** One socket owned within a spot node and its monitored status. */
export interface SpotNodeSocketEntry {
  readonly owner: SpotNodeSocketOwnerValue;
  readonly ownerId: bigint;
  readonly ownerName: string;
  readonly socketName: string;
  readonly socketType: SocketTypeValue;
  readonly autoHwmVisible: boolean;
  readonly snapshot: MonitorStatus;
}

/** Filter for a spot node peer query; omitted fields match anything. */
export interface SpotNodePeerFilter {
  readonly peerEndpoint?: string;
  readonly source?: SpotPeerSourceValue;
  readonly state?: SpotPeerStateValue;
}

/** Filter for a spot node subject query; omitted fields match anything. */
export interface SpotNodeSubjectFilter {
  readonly role?: SpotRoleValue;
  readonly subject?: string;
  readonly subjectKind?: number;
}
