import type { RoutingId } from '../Common';
import type { ZLinkSpotKind } from '../Spots';

export enum ZLinkLocationAutoConnectType {
  Invalid = 0,
  RouteMesh = 1,
  ClientServer = 2,
  DealerMesh = 3,
  Fanout = 4,
  SpotMesh = 5
}

export enum ZLinkLocationRole {
  Invalid = 0,
  Spot = 2,
  Router = 3,
  Dealer = 4,
  Pub = 5,
  Sub = 6
}

export enum ZLinkRouteKind {
  Invalid = 0,
  ActorSession = 1,
  SpotName = 2,
  FrameworkRoute = 3
}

export enum ZLinkLocationKind {
  Peer = 1,
  Spot = 2,
  Actor = 3,
  Route = 4
}

const autoConnectCanonicalNames = new Map<ZLinkLocationAutoConnectType, string>([
  [ZLinkLocationAutoConnectType.RouteMesh, 'route-mesh'],
  [ZLinkLocationAutoConnectType.ClientServer, 'client-server'],
  [ZLinkLocationAutoConnectType.DealerMesh, 'dealer-mesh'],
  [ZLinkLocationAutoConnectType.Fanout, 'fanout'],
  [ZLinkLocationAutoConnectType.SpotMesh, 'spot-mesh']
]);

const roleCanonicalNames = new Map<ZLinkLocationRole, string>([
  [ZLinkLocationRole.Router, 'router'],
  [ZLinkLocationRole.Dealer, 'dealer'],
  [ZLinkLocationRole.Pub, 'pub'],
  [ZLinkLocationRole.Sub, 'sub'],
  [ZLinkLocationRole.Spot, 'spot']
]);

const canonicalAutoConnectTypes = new Map<string, ZLinkLocationAutoConnectType>(
  [...autoConnectCanonicalNames.entries()].map(([type, name]) => [name, type])
);

const canonicalRoles = new Map<string, ZLinkLocationRole>(
  [...roleCanonicalNames.entries()].map(([role, name]) => [name, role])
);

export function zlinkLocationAutoConnectTypeName(type: ZLinkLocationAutoConnectType): string {
  const name = autoConnectCanonicalNames.get(type);
  if (name === undefined) {
    throw new RangeError(`Unknown location auto-connect type: ${type}`);
  }
  return name;
}

export function zlinkLocationRoleName(role: ZLinkLocationRole): string {
  const name = roleCanonicalNames.get(role);
  if (name === undefined) {
    throw new RangeError(`Unknown location role: ${role}`);
  }
  return name;
}

export function tryParseZLinkLocationAutoConnectType(value: string): ZLinkLocationAutoConnectType | undefined {
  return canonicalAutoConnectTypes.get(value);
}

export function tryParseZLinkLocationRole(value: string): ZLinkLocationRole | undefined {
  return canonicalRoles.get(value);
}

export interface ZLinkPeerLocation {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly nodeRid?: RoutingId;
  readonly role: ZLinkLocationRole;
  readonly endpoint: string;
  readonly weight: number;
  readonly value: bigint;
  readonly metadata?: Readonly<Record<string, string>>;
  readonly capabilities?: readonly string[];
  readonly ownerId: string;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

export interface ZLinkSpotLocation {
  readonly meshName: string;
  readonly spotRid: RoutingId;
  readonly spotType?: string;
  readonly nodeRid: RoutingId;
  readonly spotKind: ZLinkSpotKind;
  readonly routeEndpoint?: string;
  readonly ownerId: string;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

export interface ZLinkActorLocation {
  readonly actorType: string;
  readonly actorId: string;
  readonly actorRef: string;
  readonly nodeRid: RoutingId;
  readonly generation: bigint;
  readonly locationKind: ZLinkSpotKind;
  readonly spotMeshName: string;
  readonly spotRid?: RoutingId;
  readonly spotKind: ZLinkSpotKind;
  readonly ownerId: string;
  readonly updatedAt: Date;
}

export interface ZLinkRouteLocation {
  readonly routeKind: ZLinkRouteKind;
  readonly routeKey: string;
  readonly ownerNodeRid: RoutingId;
  readonly ownerId: string;
  readonly generation: bigint;
  readonly value: Uint8Array;
  readonly updatedAt: Date;
}

export interface ZLinkOwnerLease {
  readonly ownerId: string;
  readonly nodeRid: RoutingId;
  readonly leaseExpiresAt: Date;
  readonly updatedAt: Date;
}

export interface ZLinkOwnerLeaseSnapshot {
  readonly leases: readonly ZLinkOwnerLease[];
  readonly storeNow: Date;
}

export interface ZLinkPeerLocationKey {
  readonly autoConnectType: ZLinkLocationAutoConnectType;
  readonly meshName: string;
  readonly role: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly endpoint?: string;
}

export interface ZLinkSpotLocationKey {
  readonly meshName: string;
  readonly spotRid: RoutingId;
}

export interface ZLinkSpotAddress {
  readonly meshName: string;
  readonly nodeRid: RoutingId;
  readonly spotRid: RoutingId;
}

export interface ZLinkActorLocationKey {
  readonly actorType?: string;
  readonly actorId: string;
}

export interface ZLinkRouteLocationKey {
  readonly routeKind: ZLinkRouteKind;
  readonly routeKey: string;
}

export interface ZLinkPeerLocationFilter {
  readonly autoConnectType?: ZLinkLocationAutoConnectType;
  readonly meshName?: string;
  readonly role?: ZLinkLocationRole;
  readonly nodeRid?: RoutingId;
  readonly endpoint?: string;
}

export interface ZLinkSpotLocationFilter {
  readonly meshName?: string;
  readonly spotType?: string;
  readonly nodeRid?: RoutingId;
  readonly spotKind?: ZLinkSpotKind;
}

export interface ZLinkActorLocationFilter {
  readonly actorType?: string;
  readonly nodeRid?: RoutingId;
  readonly spotRid?: RoutingId;
  readonly locationKind?: ZLinkSpotKind;
}

export interface ZLinkRouteLocationFilter {
  readonly routeKind?: ZLinkRouteKind;
  readonly ownerNodeRid?: RoutingId;
  readonly ownerId?: string;
}

export interface ZLinkPageRequest {
  readonly pageSize?: number;
  readonly continuationToken?: string;
}

export interface ZLinkLocationPage<T> {
  readonly items: readonly T[];
  readonly continuationToken?: string;
}

export enum ZLinkLocationWriteIntent {
  NewClaim = 1,
  Renew = 2,
  Takeover = 3
}

export enum ZLinkLocationWriteStatus {
  Stored = 1,
  IgnoredStale = 2,
  RejectedConflict = 3,
  StoreUnavailable = 4
}

export interface ZLinkLocationWriteResult {
  readonly status: ZLinkLocationWriteStatus;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

export interface ZLinkLocationOwnerToken {
  readonly ownerId: string;
  readonly generation: bigint;
}

export interface ZLinkLocationWatchFilter {
  readonly kind: ZLinkLocationKind;
  readonly meshName?: string;
  readonly routeKind?: ZLinkRouteKind;
}

export enum ZLinkLocationChangeType {
  Upserted = 1,
  Removed = 2,
  Expired = 3
}

export interface ZLinkLocationChanged {
  readonly kind: ZLinkLocationKind;
  readonly locationKey: string;
  readonly changeType: ZLinkLocationChangeType;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

export interface ZLinkLocationChangeStampScope {
  readonly kind: ZLinkLocationKind;
  readonly meshName?: string;
}

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
  readonly spotRid?: RoutingId;
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
