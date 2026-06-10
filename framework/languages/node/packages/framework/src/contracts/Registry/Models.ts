import type { RoutingId } from '../Common';
import type { ZLinkSpotKind } from '../Spots';

export enum ZLinkAutoConnectType {
  Invalid = 0,
  RouteMesh = 1,
  ClientServer = 2,
  DealerMesh = 3,
  Fanout = 4,
  SpotMesh = 5
}

export enum ZLinkServiceKind { Discovery = 1, SpotSub = 3, SpotPub = 4, Socket = 5 }
export enum ZLinkServiceRole { Invalid = 0, Spot = 2, Router = 3, Dealer = 4, Pub = 5, Sub = 6 }
export enum ZLinkRegistryState { Idle = 1, Active = 2, Degraded = 3, Error = 4 }
export enum ZLinkTopologySource { Manual = 1, Discovery = 2, Registry = 3 }
export enum ZLinkTopologyState { Discovered = 1, Connecting = 2, Ready = 3, Lost = 4, Error = 5, Stopped = 6 }
export enum ZLinkAdmissionState { Serving = 1, Draining = 2 }

export interface ZLinkRegistryServiceSummaryFilter {
  readonly autoConnectType?: ZLinkAutoConnectType;
  readonly serviceRole?: ZLinkServiceRole;
  readonly channelName?: string;
}

export interface ZLinkRegistryTopologyFilter {
  readonly autoConnectType?: ZLinkAutoConnectType;
  readonly serviceKind?: ZLinkServiceKind;
  readonly serviceRole?: ZLinkServiceRole;
  readonly channelName?: string;
  readonly routingId?: RoutingId;
  readonly state?: ZLinkTopologyState;
  readonly source?: ZLinkTopologySource;
}

export interface ZLinkRegistryStatus {
  readonly registryId: number;
  readonly bindEndpoint: string;
  readonly state: ZLinkRegistryState;
  readonly topologyEntryCount: number;
  readonly peerRegistryCount: number;
  readonly connectedPeerRegistryCount: number;
  readonly listSeq: bigint;
  readonly lastError: number;
  readonly lastChangedMs: bigint;
}

export interface ZLinkRegistryServiceSummaryEntry {
  readonly autoConnectType: ZLinkAutoConnectType;
  readonly serviceRole: ZLinkServiceRole;
  readonly channelName: string;
  readonly totalCount: number;
  readonly connectingCount: number;
  readonly readyCount: number;
  readonly errorCount: number;
  readonly stoppedCount: number;
  readonly lastReportedMs: bigint;
}

export interface ZLinkRegistryTopologyEntry {
  readonly autoConnectType: ZLinkAutoConnectType;
  readonly routingId?: RoutingId;
  readonly serviceKind: ZLinkServiceKind;
  readonly serviceRole: ZLinkServiceRole;
  readonly channelName: string;
  readonly endpoint: string;
  readonly source: ZLinkTopologySource;
  readonly state: ZLinkTopologyState;
  readonly desiredCount: number;
  readonly readyCount: number;
  readonly errorCode: number;
  readonly lastReportedMs: bigint;
  readonly spotKind: ZLinkSpotKind;
}

export interface ZLinkMemberPeerEntry {
  readonly autoConnectType: ZLinkAutoConnectType;
  readonly serviceRole: ZLinkServiceRole;
  readonly channelName: string;
  readonly endpoint: string;
  readonly routingId?: RoutingId;
  readonly value: bigint;
  readonly weight: number;
}
