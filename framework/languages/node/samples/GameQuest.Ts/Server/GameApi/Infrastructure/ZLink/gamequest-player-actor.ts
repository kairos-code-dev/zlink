import type { ZLinkActor, ZLinkActorContext } from '@zlink-systems/framework';

class GameQuestPlayerActor implements ZLinkActor {
  constructor(
    readonly actorId: string,
    readonly context: ZLinkActorContext
  ) {}

  async push(message: unknown): Promise<void> {
    try {
      await this.context.boundSession.send(message).submit();
    } catch (error) {
      // The quest state is already recorded. A disconnected player restores it on join.
      console.error(`gamequest bound push skipped actor=${this.actorId} error=${error instanceof Error ? error.message : String(error)}`);
    }
  }
}

export { GameQuestPlayerActor };
