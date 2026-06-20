import type { ActorRef, RoutingId, Type } from '../Common';
import type { ZLinkBoundSession } from '../Streams';
import type { ZLinkSpot } from '../Spots';
import type { ZLinkActorJoinEntrySpotCall, ZLinkActorJoinSpotCall } from './ZLinkActorFactory';

export interface ZLinkActorContext {
  readonly spotRid?: RoutingId;
  readonly actorRef?: ActorRef;
  readonly isJoined: boolean;
  readonly boundSession: ZLinkBoundSession;
  getSpot(): ZLinkSpot;
  getSpot<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): TSpot;
  joinSpot(spotRid: RoutingId, request?: unknown): ZLinkActorJoinSpotCall;
  joinEntrySpot(nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall;
}
