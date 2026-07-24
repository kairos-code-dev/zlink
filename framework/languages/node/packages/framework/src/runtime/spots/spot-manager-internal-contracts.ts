import type { RoutingId, ZLinkSpotCreateState } from '../../contracts';

export interface ZLinkLocalSpotCreateResult {
  readonly spotRid: RoutingId;
  readonly state: ZLinkSpotCreateState;
  readonly reply?: unknown;
  readonly publication?: {
    publish(): void;
    abort(): void;
  };
}
