import type { ZLinkActor } from '../../contracts';
import type {
  ZLinkBackendActorRef,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import type { ZLinkActorManagerOptions } from './actor-runtime-contracts';
import type { ZLinkActorRuntimeState } from './actor-runtime-state';
import { ZLinkActorRetryDelay } from './actor-retry-delay';

type ZLinkTransferredActorRollbackOptions = Pick<
  ZLinkActorManagerOptions,
  | 'actorDestroyedCleanup'
  | 'nativeActorNode'
  | 'nativeActorNodeProvider'
  | 'shutdownSignal'
>;

export class ZLinkTransferredActorRollbackCoordinator {
  private readonly tasks = new Map<string, Promise<void>>();

  constructor(
    private readonly states: Map<string, ZLinkActorRuntimeState>,
    private readonly options: ZLinkTransferredActorRollbackOptions
  ) {}

  async rollback(actor: ZLinkActor, signal?: AbortSignal): Promise<void> {
    const state = this.states.get(actor.actorId);
    if (state?.actor !== actor) {
      return;
    }

    state.clearJoinedSpot();
    if (!state.isMoving) {
      state.beginMove();
    }

    const actorRef = state.nativeActorRef;
    const node = this.options.nativeActorNode ?? this.options.nativeActorNodeProvider?.();
    try {
      if (node !== undefined && actorRef !== undefined) {
        await node.destroyActor(actorRef, 0, signal);
      }
    } catch (error) {
      this.schedule(actor, state, node, actorRef);
      throw error;
    }
    this.complete(actor, state);
  }

  private schedule(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    node: ZLinkBackendSpotNode | undefined,
    actorRef: ZLinkBackendActorRef | undefined
  ): void {
    if (this.tasks.has(actor.actorId)) {
      return;
    }
    const task = this.retry(actor, state, node, actorRef)
      .finally(() => this.tasks.delete(actor.actorId));
    this.tasks.set(actor.actorId, task);
  }

  private async retry(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    node: ZLinkBackendSpotNode | undefined,
    actorRef: ZLinkBackendActorRef | undefined
  ): Promise<void> {
    const retryDelay = new ZLinkActorRetryDelay();
    while (this.states.get(actor.actorId) === state && this.options.shutdownSignal?.aborted !== true) {
      if (!await retryDelay.wait(this.options.shutdownSignal)) return;
      try {
        if (node !== undefined && actorRef !== undefined) {
          await node.destroyActor(actorRef, 0);
        }
        this.complete(actor, state);
        return;
      } catch {
        // The tombstone prevents dispatch while the next retry is pending.
      }
    }
  }

  private complete(actor: ZLinkActor, state: ZLinkActorRuntimeState): void {
    if (this.states.get(actor.actorId) !== state) {
      return;
    }
    this.options.actorDestroyedCleanup?.(actor.actorId);
    state.clearAfterDestroy();
    this.states.delete(actor.actorId);
  }
}
