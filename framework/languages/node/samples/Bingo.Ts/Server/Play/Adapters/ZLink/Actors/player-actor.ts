import type {
  ZLinkActor,
  ZLinkActorContext
} from '@zlink-systems/framework';

class PlayerActor implements ZLinkActor {
  readonly context!: ZLinkActorContext;
  private nextSeq = 0;

  constructor(
    readonly actorId: string,
    public displayName: string,
    public destroyAfterEntrySpotJoin = false,
    public disconnected = false
  ) {
    this.actorId = actorId;
    this.displayName = displayName;
  }

  markForDestroyAfterRoomLeave(): void {
    this.destroyAfterEntrySpotJoin = true;
  }

  markDisconnected(): void {
    this.disconnected = true;
  }

  async push(packetName: string, payload: unknown): Promise<void> {
    this.nextSeq += 1;
    if (process.env.BINGO_DEBUG_FLOW === '1') {
      console.log(`play-actor-push actor=${this.actorId} packet=${packetName} seq=${this.nextSeq}`);
    }
    await this.context.boundSession
      .send(payload)
      .packetName(packetName)
      .metadata('seq', String(this.nextSeq))
      .submit();
  }
}

export { PlayerActor };
