import type { ZLinkMessage, ZLinkSpot, ZLinkSpotContext } from '@zlink-systems/framework';

class PlayerQuestSpot implements ZLinkSpot {
  readonly context!: ZLinkSpotContext;
  private playerId = '';

  async onCreate(request: ZLinkMessage): Promise<{ accepted: boolean }> {
    const payload = request.decode<{ playerId?: string }>(Object as never);
    this.playerId = typeof payload.playerId === 'string' ? payload.playerId : String(this.context.spotRid);
    console.error(`gamequest player quest spot ready player=${this.playerId} spot=${String(this.context.spotRid)}`);
    return { accepted: true };
  }
}

export {
  PlayerQuestSpot
};
