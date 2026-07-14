import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkSpot
} from '../../contracts';
import type { ZLinkActorJoinRuntimeResult } from './actor-runtime-contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendSpotNode } from '../backend';
import type { ZLinkLocationLifecycle } from '../locations';
import type { DefaultZLinkSpotManager } from '../spots';
import type { ZLinkActorJoinCoordinator } from './actor-runtime-contracts';
import type { ZLinkActorRuntimeState } from './actor-runtime-state';
import { ZLinkPostCommitActorBinder } from './post-commit-actor-binder';
import { routingIdsEqual } from '../routing-id';

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
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    const localSpotManager = this.options.localSpotManager();
    if (
      localSpotManager === undefined ||
      !localSpotManager.hasActiveSpot(spotRid)
    ) {
      return this.options.native.joinSpot(actor, state, spotRid, request, timeoutMs, signal);
    }
    const sourceSpotRid = state.spotRid;
    const sourceSpot = state.spot;
    const movesFromLocalUserSpot = sourceSpotRid !== undefined
      && !routingIdsEqual(sourceSpotRid, spotRid)
      && localSpotManager.hasActiveSpot(sourceSpotRid);
    let sourcePrepared = false;
    let result: Awaited<ReturnType<DefaultZLinkSpotManager['admitActorJoin']>>;
    try {
      result = await localSpotManager.admitActorJoin(
        spotRid,
        actor,
        request,
        (spot: ZLinkSpot) => {
          state.setJoinedSpot(spotRid, spot);
          return () => {
            if (sourceSpotRid !== undefined && sourceSpot !== undefined) {
              state.setJoinedSpot(sourceSpotRid, sourceSpot);
            } else {
              state.clearJoinedSpot();
            }
          };
        },
        signal,
        movesFromLocalUserSpot
          ? async () => {
              await localSpotManager.beginActorTransfer(sourceSpotRid, actor.actorId);
              try {
                await localSpotManager.prepareActorLeaveForTransfer(sourceSpotRid, actor, signal);
                sourcePrepared = true;
              } catch (error) {
                await localSpotManager.cancelActorTransfer(sourceSpotRid, actor.actorId);
                throw error;
              }
            }
          : undefined
      );
    } catch (error) {
      if (sourcePrepared && sourceSpotRid !== undefined) {
        await localSpotManager.restoreActorAfterFailedTransfer(sourceSpotRid, actor, signal);
      }
      throw error;
    }
    if (result.accepted && sourcePrepared && sourceSpotRid !== undefined) {
      try {
        await localSpotManager.commitActorLeaveAfterTransfer(sourceSpotRid, actor.actorId);
      } catch (error) {
        this.options.postCommitErrorReporter?.(error);
      }
    }
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
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    return this.options.native.joinEntrySpot(actor, state, nodeRid, request, timeoutMs, signal);
  }
}

function nodeRidForLocalActor(nodeProvider: () => ZLinkBackendSpotNode): RoutingId {
  return nodeProvider().routingId;
}

function localActorRef(nodeRid: RoutingId, actorId: string): ActorRef {
  return { nodeRid, actorId, generation: 0n } as ActorRef;
}
