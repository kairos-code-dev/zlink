import type { RoutingId } from '../../contracts';
import type { ZLinkLocationLifecycle } from '../locations';

export interface ZLinkPostCommitActorLocationOptions {
  readonly lifecycle: ZLinkLocationLifecycle;
  readonly reportError?: (error: unknown) => void;
  readonly signal?: AbortSignal;
}

export class ZLinkPostCommitActorLocation {
  private readonly queues = new Map<string, Array<() => Promise<void>>>();
  private readonly tasks = new Map<string, Promise<void>>();

  constructor(private readonly options: ZLinkPostCommitActorLocationOptions) {}

  joinedEventually(actorType: string, actorId: string, meshName: string, spotRid: RoutingId): void {
    this.enqueue(actorId, () => this.options.lifecycle.notifyActorJoinedSpot(
      actorType,
      actorId,
      meshName,
      spotRid
    ));
  }

  leftEventually(actorType: string, actorId: string): void {
    this.enqueue(actorId, () => this.options.lifecycle.notifyActorLeftSpot(actorType, actorId));
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
    let retryDelayMs = 25;
    while (this.options.signal?.aborted !== true) {
      const operation = this.queues.get(actorId)?.[0];
      if (operation === undefined) {
        this.queues.delete(actorId);
        return;
      }
      try {
        await operation();
        this.queues.get(actorId)?.shift();
        retryDelayMs = 25;
      } catch (error) {
        this.options.reportError?.(error);
        if (!await delayUnlessAborted(retryDelayMs, this.options.signal)) {
          return;
        }
        retryDelayMs = Math.min(retryDelayMs * 2, 1_000);
      }
    }
  }
}

function delayUnlessAborted(delayMs: number, signal: AbortSignal | undefined): Promise<boolean> {
  if (signal?.aborted === true) return Promise.resolve(false);
  return new Promise((resolve) => {
    const deadline = setTimeout(() => {
      signal?.removeEventListener('abort', aborted);
      resolve(true);
    }, delayMs);
    deadline.unref();
    const aborted = (): void => {
      clearTimeout(deadline);
      resolve(false);
    };
    signal?.addEventListener('abort', aborted, { once: true });
  });
}
