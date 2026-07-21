import type { ZLinkLocationOptionValues } from '../RouteMesh';

export interface ZLinkLocationOptions {
  heartbeatIntervalMs(value: number): this;
  ownerLeaseTtlMs(value: number): this;
  pollingIntervalMs(value: number): this;
  storeFailureGraceMs(value: number): this;
  routingIdFencingMarginMs(value: number): this;
  ownerLeaseRenewTimeoutMs(value: number): this;
}

export type ZLinkLocationOptionOverrides =
  Partial<ZLinkLocationOptionValues> & { readonly listPageSize?: number };

export const zlinkRuntimeDefaultLocationOptions: Readonly<
  ZLinkLocationOptionValues & { readonly listPageSize: number }
> = {
  heartbeatIntervalMs: 10000,
  ownerLeaseTtlMs: 30000,
  pollingIntervalMs: 1000,
  listPageSize: 1000,
  storeFailureGraceMs: 30000,
  routingIdFencingMarginMs: 5000,
  ownerLeaseRenewTimeoutMs: 3000
};

export const zlinkDefaultLocationOptions: Readonly<ZLinkLocationOptionValues> =
  zlinkRuntimeDefaultLocationOptions;
