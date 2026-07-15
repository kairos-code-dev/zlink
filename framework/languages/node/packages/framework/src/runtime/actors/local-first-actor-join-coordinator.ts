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
    const sourceTransfer = new LocalActorSourceTransfer(
      localSpotManager,
      movesFromLocalUserSpot ? sourceSpotRid : undefined,
      actor,
      signal
    );
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
          ? () => sourceTransfer.prepare()
          : undefined
      );
    } catch (error) {
      await sourceTransfer.restoreIfPrepared();
      throw error;
    }
    if (result.accepted) {
      try {
        await sourceTransfer.commitIfPrepared();
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

class LocalActorSourceTransfer {
  private prepared = false;

  constructor(
    private readonly spotManager: DefaultZLinkSpotManager,
    private readonly sourceSpotRid: RoutingId | undefined,
    private readonly actor: ZLinkActor,
    private readonly signal: AbortSignal | undefined
  ) {}

  async prepare(): Promise<void> {
    if (this.sourceSpotRid === undefined) {
      return;
    }
    await this.spotManager.beginActorTransfer(this.sourceSpotRid, this.actor.actorId);
    try {
      await this.spotManager.prepareActorLeaveForTransfer(this.sourceSpotRid, this.actor, this.signal);
      this.prepared = true;
    } catch (error) {
      await this.spotManager.cancelActorTransfer(this.sourceSpotRid, this.actor.actorId);
      throw error;
    }
  }

  async restoreIfPrepared(): Promise<void> {
    if (!this.prepared || this.sourceSpotRid === undefined) {
      return;
    }
    await this.spotManager.restoreActorAfterFailedTransfer(this.sourceSpotRid, this.actor, this.signal);
  }

  async commitIfPrepared(): Promise<void> {
    if (!this.prepared || this.sourceSpotRid === undefined) {
      return;
    }
    await this.spotManager.commitActorLeaveAfterTransfer(this.sourceSpotRid, this.actor.actorId);
  }
}

function nodeRidForLocalActor(nodeProvider: () => ZLinkBackendSpotNode): RoutingId {
  return nodeProvider().routingId;
}

function localActorRef(nodeRid: RoutingId, actorId: string): ActorRef {
  return { nodeRid, actorId, generation: 0n } as ActorRef;
}
