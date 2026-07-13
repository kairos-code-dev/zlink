import type { RoutingId } from '../Common';
import type { ZLinkBoundSession } from '../Streams';
import type { ZLinkActorJoinEntrySpotCall, ZLinkActorJoinSpotCall } from './ZLinkActorFactory';

export interface ZLinkActorContext {
  readonly spotRid?: RoutingId;
  readonly boundSession: ZLinkBoundSession;
  joinSpot(spotRid: RoutingId, request: unknown): ZLinkActorJoinSpotCall;
  joinEntrySpot(nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall;
}
