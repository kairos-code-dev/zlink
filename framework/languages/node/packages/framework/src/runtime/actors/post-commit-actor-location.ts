import type { RoutingId } from '../../contracts';
import type { ZLinkLocationLifecycle } from '../locations';
import { ZLinkActorRetryDelay } from './actor-retry-delay';

export interface ZLinkPostCommitActorLocationOptions {
  readonly lifecycle: ZLinkLocationLifecycle;
  readonly reportError?: (error: unknown) => void;
  readonly signal?: AbortSignal;
}

export class ZLinkPostCommitActorLocation {
  private readonly queues = new Map<string, Array<() => Promise<void>>>();
  private readonly tasks = new Map<string, Promise<void>>();

  constructor(private readonly options: ZLinkPostCommitActorLocationOptions) {}

  joinedEventually(
    actorType: string,
    actorId: string,
    meshName: string,
    spotId: RoutingId,
    spotGeneration: bigint,
    membershipEpoch: bigint,
    ownerNodeGeneration: bigint
  ): void {
    this.enqueue(actorId, () => this.options.lifecycle.notifyActorJoinedSpot(
      actorType,
      actorId,
      meshName,
      spotId,
      spotGeneration,
      membershipEpoch,
      ownerNodeGeneration
    ));
  }

  leftEventually(
    actorType: string,
    actorId: string,
    entrySpotId: RoutingId,
    entrySpotGeneration: bigint,
    membershipEpoch: bigint,
    ownerNodeGeneration: bigint
  ): void {
    this.enqueue(actorId, () => this.options.lifecycle.notifyActorLeftSpot(
      actorType,
      actorId,
      entrySpotId,
      entrySpotGeneration,
      membershipEpoch,
      ownerNodeGeneration
    ));
  }

  private enqueue(actorId: string, operation: () => Promise<void>): void {
    const queue = this.queues.get(actorId) ?? [];
    queue.push(operation);
    this.queues.set(actorId, queue);
    if (this.tasks.has(actorId)) {
      return;
    }
    const task = this.run(actorId).finally(() => this.tasks.delete(actorId));
    this.tasks.set(actorId, task);
  }

  private async run(actorId: string): Promise<void> {
    const retryDelay = new ZLinkActorRetryDelay();
    while (this.options.signal?.aborted !== true) {
      const operation = this.queues.get(actorId)?.[0];
      if (operation === undefined) {
        this.queues.delete(actorId);
        return;
      }
      try {
        await operation();
        this.queues.get(actorId)?.shift();
        retryDelay.reset();
      } catch (error) {
        this.options.reportError?.(error);
        if (!await retryDelay.wait(this.options.signal)) {
          return;
        }
      }
    }
  }
}
