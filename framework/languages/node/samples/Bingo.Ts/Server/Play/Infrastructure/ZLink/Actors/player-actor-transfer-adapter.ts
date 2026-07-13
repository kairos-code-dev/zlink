import { ZLinkMessage } from '@zlink-systems/framework';
import type { ZLinkActorTransferAdapter } from '@zlink-systems/framework';
import { PlayerActor } from './player-actor';
import { PlayerActorTransferState } from '../../../../../Shared/Contracts/bingo-messages.generated';

class PlayerActorTransferAdapter implements ZLinkActorTransferAdapter<PlayerActor> {
  async transferOut(actor: PlayerActor): Promise<ZLinkMessage> {
    return ZLinkMessage.from(new PlayerActorTransferState({
      displayName: actor.displayName,
      destroyAfterEntrySpotJoin: actor.destroyAfterEntrySpotJoin,
      disconnected: actor.disconnected
    }));
  }

  async transferIn(actorId: string, state: ZLinkMessage): Promise<PlayerActor> {
    const restored = state.decode<PlayerActorTransferState>();
    return new PlayerActor(
      actorId,
      restored.displayName,
      restored.destroyAfterEntrySpotJoin,
      restored.disconnected
    );
  }
}

export { PlayerActorTransferAdapter };
