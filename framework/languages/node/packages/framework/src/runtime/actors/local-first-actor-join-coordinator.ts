import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkActorJoinResult,
  ZLinkSpot
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendSpotNode } from '../backend';
import type { ZLinkLocationLifecycle } from '../locations';
import type { DefaultZLinkSpotManager } from '../spots';
import type { ZLinkActorJoinCoordinator } from './index';
import type { ZLinkActorRuntimeState } from './actor-runtime-state';
import { ZLinkPostCommitActorBinder } from './post-commit-actor-binder';

export interface ZLinkLocalFirstActorJoinCoordinatorOptions {
  readonly localSpotManager: () => DefaultZLinkSpotManager | undefined;
  readonly nativeNode: () => ZLinkBackendSpotNode;
  readonly native: ZLinkActorJoinCoordinator;
  readonly actorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>;
  readonly postCommitErrorReporter?: (error: unknown) => void;
  readonly locationLifecycle?: () => ZLinkLocationLifecycle | undefined;
  readonly localSpotMeshName?: () => string | undefined;
}

export class ZLinkLocalFirstActorJoinCoordinator implements ZLinkActorJoinCoordinator {
  private readonly postCommitBinder: ZLinkPostCommitActorBinder | undefined;

  constructor(private readonly options: ZLinkLocalFirstActorJoinCoordinatorOptions) {
    this.postCommitBinder = options.actorBinder === undefined
      ? undefined
      : new ZLinkPostCommitActorBinder({
          bind: (actorRef, force) => options.actorBinder!(actorRef, undefined, force),
          reportError: options.postCommitErrorReporter
        });
  }

  async joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    const localSpotManager = this.options.localSpotManager();
    if (
      localSpotManager === undefined ||
      !localSpotManager.hasActiveSpot(spotRid)
    ) {
      return this.options.native.joinSpot(actor, state, spotRid, request, timeoutMs, signal);
    }
    const result = await localSpotManager.admitActorJoin(
      spotRid,
      actor,
      request,
      (spot: ZLinkSpot) => {
        state.setJoinedSpot(spotRid, spot);
        return () => state.clearJoinedSpot();
      },
      signal
    );
    const nativeActorRef = state.nativeActorRef;
    const actorRef = nativeActorRef === undefined
      ? localActorRef(nodeRidForLocalActor(this.options.nativeNode), actor.actorId)
      : {
          nodeRid: nativeActorRef.nodeRid as unknown as RoutingId,
          actorId: nativeActorRef.actorId,
          generation: nativeActorRef.generation
        } as ActorRef;
    if (result.accepted) {
      const actorType = state.actorType;
      const meshName = this.options.localSpotMeshName?.();
      if (actorType !== undefined && meshName !== undefined) {
        await this.options.locationLifecycle?.()?.notifyActorJoinedSpot(
          actorType,
          actor.actorId,
          meshName,
          spotRid
        );
      }
      this.postCommitBinder?.bindEventually(actorRef);
    }
    return {
      accepted: result.accepted,
      actor: result.accepted ? actorRef : undefined,
      reply: result.reply as Message | undefined
    };
  }

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinResult<Message>> {
    return this.options.native.joinEntrySpot(actor, state, nodeRid, request, timeoutMs, signal);
  }
}

function nodeRidForLocalActor(nodeProvider: () => ZLinkBackendSpotNode): RoutingId {
  return nodeProvider().routingId;
}

function localActorRef(nodeRid: RoutingId, actorId: string): ActorRef {
  return { nodeRid, actorId, generation: 0n } as ActorRef;
}
