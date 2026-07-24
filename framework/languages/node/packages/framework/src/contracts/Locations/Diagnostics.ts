import type { RoutingId, SpotId } from '../Common';
import type { ZLinkLocationAutoConnectType, ZLinkLocationKind, ZLinkLocationRole } from './Values';

export interface ZLinkLocationRuntimeStatus {
  readonly storeHealthy: boolean;
  readonly watchEnabled: boolean;
  readonly pollingIntervalMs: number;
  readonly lastRefreshAt?: Date;
  readonly lastError?: string;
  readonly ownerLeaseHealthy: boolean;
  readonly ownerLeaseRenewedAt?: Date;
}

export enum ZLinkLocationTopologyState {
  Discovered = 1,
  Connecting = 2,
  Ready = 3,
  Lost = 4,
  Error = 5,
  Stopped = 6
}

export interface ZLinkLocationTopologyFilter {
  readonly kind?: ZLinkLocationKind;
  readonly meshName?: string;
  readonly role?: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly state?: ZLinkLocationTopologyState;
}

export interface ZLinkLocationTopologyEntry {
  readonly kind: ZLinkLocationKind;
  readonly meshName?: string;
  readonly role?: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly spotId?: SpotId;
  readonly actorId?: string;
  readonly endpoint?: string;
  readonly state: ZLinkLocationTopologyState;
  readonly desiredCount: number;
  readonly readyCount: number;
  readonly errorCode: number;
  readonly updatedAt: Date;
}

export interface ZLinkLocationServiceSummaryFilter {
  readonly meshName?: string;
  readonly autoConnectType?: ZLinkLocationAutoConnectType;
  readonly role?: ZLinkLocationRole;
}

export interface ZLinkLocationServiceSummary {
  readonly meshName: string;
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly role: ZLinkLocationRole;
  readonly totalCount: number;
  readonly readyCount: number;
  readonly errorCount: number;
  readonly stoppedCount: number;
  readonly updatedAt: Date;
}
