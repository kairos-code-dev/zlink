// SPDX-License-Identifier: MPL-2.0

import type { RoutingId } from '../../core';
import type { MonitorStatus } from '../../eventing';
import type { SocketTypeValue } from '../../sockets/socket_constants';
import type {
  AutoConnectType,
  ServiceKindValue,
  ServiceRoleValue,
} from '../discovery/discovery_models';
import type {
  SpotKindValue,
  SpotNodeSocketOwnerValue,
  SpotNodeStateValue,
  SpotPeerKindValue,
  SpotPeerSourceValue,
  SpotPeerStateValue,
  SpotRoleValue,
} from '../spot/spot_models';

export const RegistryState = Object.freeze({ Idle: 1, Active: 2, Degraded: 3, Error: 4 } as const);
export type RegistryStateValue = typeof RegistryState[keyof typeof RegistryState];
export const TopologySource = Object.freeze({ Manual: 1, Discovery: 2, Registry: 3 } as const);
export type TopologySourceValue = typeof TopologySource[keyof typeof TopologySource];
export const TopologyState = Object.freeze({ Discovered: 1, Connecting: 2, Ready: 3, Lost: 4, Error: 5, Stopped: 6 } as const);
export type TopologyStateValue = typeof TopologyState[keyof typeof TopologyState];
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
  readonly spotKind: SpotKindValue;
}

export interface SpotRoute {
  readonly spotRid: RoutingId;
  readonly ownerNodeRid: RoutingId;
  readonly spotKind: SpotKindValue;
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
  readonly kind: SpotPeerKindValue;
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

export interface SpotNodeSocketFilter {
  readonly owner?: SpotNodeSocketOwnerValue;
  readonly socketType?: SocketTypeValue;
  readonly socketName?: string;
}

export interface SpotNodeSocketEntry {
  readonly owner: SpotNodeSocketOwnerValue;
  readonly ownerId: bigint;
  readonly ownerName: string;
  readonly socketName: string;
  readonly socketType: SocketTypeValue;
  readonly autoHwmVisible: boolean;
  readonly snapshot: MonitorStatus;
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
