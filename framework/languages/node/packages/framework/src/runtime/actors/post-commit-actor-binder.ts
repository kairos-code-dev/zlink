import type { ActorRef } from '../../contracts';

export interface ZLinkPostCommitActorBinderOptions {
  readonly bind: (actorRef: ActorRef, force: boolean) => Promise<void>;
  readonly reportError?: (error: unknown) => void;
}

export class ZLinkPostCommitActorBinder {
  private readonly desiredRefs = new Map<string, ActorRef>();
  private readonly tasks = new Map<string, Promise<void>>();

  constructor(private readonly options: ZLinkPostCommitActorBinderOptions) {}

  bindEventually(actorRef: ActorRef, force = true): void {
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
        await new Promise((resolve) => setTimeout(resolve, retryDelayMs));
        retryDelayMs = Math.min(retryDelayMs * 2, 1_000);
      }
    }
  }
}
