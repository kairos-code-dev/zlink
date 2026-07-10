import type { ActorRef } from '../../contracts';

export interface ZLinkPostCommitActorBinderOptions {
  readonly bind: (actorRef: ActorRef, force: boolean) => Promise<void>;
  readonly reportError?: (error: unknown) => void;
  readonly signal?: AbortSignal;
}

export class ZLinkPostCommitActorBinder {
  private readonly desiredRefs = new Map<string, ActorRef>();
  private readonly tasks = new Map<string, Promise<void>>();

  constructor(private readonly options: ZLinkPostCommitActorBinderOptions) {}

  bindEventually(actorRef: ActorRef): void {
    this.desiredRefs.set(actorRef.actorId, actorRef);
    if (this.tasks.has(actorRef.actorId)) {
      return;
    }
    const task = this.run(actorRef.actorId)
      .finally(() => this.tasks.delete(actorRef.actorId));
    this.tasks.set(actorRef.actorId, task);
  }

  private async run(actorId: string): Promise<void> {
    let retryDelayMs = 25;
    for (;;) {
      if (this.options.signal?.aborted === true) return;
      const actorRef = this.desiredRefs.get(actorId);
      if (actorRef === undefined) {
        return;
      }
      try {
        await this.options.bind(actorRef, true);
        if (this.desiredRefs.get(actorId) === actorRef) {
          this.desiredRefs.delete(actorId);
          return;
        }
        retryDelayMs = 25;
      } catch (error) {
        this.options.reportError?.(error);
        if (!await delayUnlessAborted(retryDelayMs, this.options.signal)) return;
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
