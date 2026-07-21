import type { RoutingId } from '../Common';
import type { ZLinkBoundSession } from '../Streams';
import type { ZLinkActorHandlerRegistry } from '../Spots';
import type { ZLinkActorJoinEntrySpotCall, ZLinkActorJoinSpotCall } from './ZLinkActorFactory';

export interface ZLinkActorContext {
  readonly meshName: string;
  readonly spotRid?: RoutingId;
  readonly handlers: ZLinkActorHandlerRegistry;
  readonly boundSession: ZLinkBoundSession;
  joinSpot(spotRid: RoutingId, request: unknown): ZLinkActorJoinSpotCall;
  joinEntrySpot(nodeRid: RoutingId, request: unknown): ZLinkActorJoinEntrySpotCall;
  leaveSpot(signal?: AbortSignal): Promise<void>;
}
