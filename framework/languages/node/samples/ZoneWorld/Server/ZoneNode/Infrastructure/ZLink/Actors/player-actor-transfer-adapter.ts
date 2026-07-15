import { ZLinkMessage } from '@zlink-systems/framework';
import type { ZLinkActorTransferAdapter } from '@zlink-systems/framework';
import { PlayerActor } from './player-actor';
import type { ZoneId } from '../../../../../Shared/spec';

type PlayerTransferState = {
  x: number;
  y: number;
  zoneId: ZoneId;
  isBot: boolean;
  dirX: number;
  dirY: number;
};

class PlayerActorTransferAdapter implements ZLinkActorTransferAdapter<PlayerActor> {
  async transferOut(actor: PlayerActor): Promise<ZLinkMessage> {
    return ZLinkMessage.from<PlayerTransferState>({
      x: actor.x,
      y: actor.y,
      zoneId: actor.zoneId,
      isBot: actor.isBot,
      dirX: actor.dirX,
      dirY: actor.dirY
    });
  }

  async transferIn(actorId: string, message: ZLinkMessage): Promise<PlayerActor> {
    const state = message.decode<PlayerTransferState>(Object as never);
    return new PlayerActor(actorId, state.x, state.y, state.zoneId, state.isBot, state.dirX, state.dirY);
  }
}

export { PlayerActorTransferAdapter };
