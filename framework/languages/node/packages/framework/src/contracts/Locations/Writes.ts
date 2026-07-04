import type { RoutingId } from '../Common';

export enum ZLinkLocationWriteIntent {
  NewClaim = 1,
  Renew = 2,
  Takeover = 3
}

export enum ZLinkLocationWriteStatus {
  Stored = 1,
  IgnoredStale = 2,
  RejectedConflict = 3
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

export interface ZLinkOwnerLeaseRenewal {
  readonly leaseExpiresAt: Date;
  readonly storeNow: Date;
}

export interface ZLinkOwnerLeaseRenewalRequest {
  readonly ownerId: string;
  readonly nodeRid: RoutingId;
  readonly leaseTtlMs: number;
}
