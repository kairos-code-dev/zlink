export interface ZLinkLocationOptions {
  readonly heartbeatIntervalMs?: number;
  readonly ownerLeaseTtlMs?: number;
  readonly pollingIntervalMs?: number;
  readonly listPageSize?: number;
  readonly storeFailureGraceMs?: number;
}

export const zlinkDefaultLocationOptions: Required<ZLinkLocationOptions> = {
  heartbeatIntervalMs: 5000,
  ownerLeaseTtlMs: 15000,
  pollingIntervalMs: 1000,
  listPageSize: 1000,
  storeFailureGraceMs: 30000
};
