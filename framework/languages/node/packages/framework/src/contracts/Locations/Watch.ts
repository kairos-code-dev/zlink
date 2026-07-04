import type { ZLinkLocationKey } from './Keys';
import type { ZLinkLocationKind, ZLinkRouteKind } from './Values';

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
  readonly key: ZLinkLocationKey;
  readonly changeType: ZLinkLocationChangeType;
  readonly generation: bigint;
  readonly updatedAt: Date;
}

export interface ZLinkLocationChangeStampScope {
  readonly kind: ZLinkLocationKind;
  readonly meshName?: string;
}
