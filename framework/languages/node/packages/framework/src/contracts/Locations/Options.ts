export interface ZLinkLocationOptions {
  readonly heartbeatIntervalMs?: number;
  readonly ownerLeaseTtlMs?: number;
  readonly pollingIntervalMs?: number;
  readonly listPageSize?: number;
  readonly storeFailureGraceMs?: number;
  readonly routingIdFencingMarginMs?: number;
  readonly ownerLeaseRenewTimeoutMs?: number;
}

export const zlinkDefaultLocationOptions: Required<ZLinkLocationOptions> = {
  heartbeatIntervalMs: 10000,
  ownerLeaseTtlMs: 30000,
  pollingIntervalMs: 1000,
  listPageSize: 1000,
  storeFailureGraceMs: 30000,
  routingIdFencingMarginMs: 5000,
  ownerLeaseRenewTimeoutMs: 3000
};
