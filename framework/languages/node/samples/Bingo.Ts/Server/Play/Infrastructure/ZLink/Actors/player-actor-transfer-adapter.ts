import { ZLinkMessage } from '@zlink-systems/framework';
import type { ZLinkActorTransferAdapter } from '@zlink-systems/framework';
import { PlayerActor } from './player-actor';

interface PlayerTransferState {
  readonly displayName: string;
  readonly destroyAfterEntrySpotJoin: boolean;
  readonly disconnected: boolean;
}

class PlayerActorTransferAdapter implements ZLinkActorTransferAdapter<PlayerActor> {
  async transferOut(actor: PlayerActor): Promise<ZLinkMessage> {
    return ZLinkMessage.from<PlayerTransferState>({
      displayName: actor.displayName,
      destroyAfterEntrySpotJoin: actor.destroyAfterEntrySpotJoin,
      disconnected: actor.disconnected
    });
  }

  async transferIn(actorId: string, state: ZLinkMessage): Promise<PlayerActor> {
    const restored = state.decode<PlayerTransferState>();
    return new PlayerActor(
      actorId,
      restored.displayName,
      restored.destroyAfterEntrySpotJoin,
      restored.disconnected
    );
  }
}

export { PlayerActorTransferAdapter };
