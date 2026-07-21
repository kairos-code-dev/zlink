import type { ActorRef, RoutingId } from '../Common';

export interface ZLinkActorPlacement {
  readonly preferredNodeRid?: RoutingId;
  readonly routeMesh?: string;
}

export interface ZLinkActorDirectory {
  find(meshName: string, actorId: string, signal?: AbortSignal): Promise<ActorRef | undefined>;
  ensure(
    meshName: string,
    actorId: string,
    createRequest: unknown,
    placement?: ZLinkActorPlacement,
    signal?: AbortSignal
  ): Promise<ActorRef>;
}
