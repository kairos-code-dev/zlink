import type { RoutingId, ZLinkSpotCreateState } from '../../contracts';

export interface ZLinkLocalSpotCreateResult {
  readonly spotRid: RoutingId;
  readonly state: ZLinkSpotCreateState;
  readonly reply?: unknown;
}
