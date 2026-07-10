import { ZLinkMessage } from '@zlink-systems/framework';
import type { ZLinkActorTransferAdapter } from '@zlink-systems/framework';
import { PlayActor } from './play-actor';

interface PlayActorTransferState {
  readonly displayName: string;
  readonly level: number;
  readonly wins: number;
  readonly roomId?: string;
  readonly destroyAfterEntrySpotJoin: boolean;
}

class PlayActorTransferAdapter implements ZLinkActorTransferAdapter<PlayActor> {
  async transferOut(actor: PlayActor): Promise<ZLinkMessage> {
    return ZLinkMessage.from<PlayActorTransferState>({
      displayName: actor.displayName,
      level: actor.level,
      wins: actor.wins,
      roomId: actor.roomId,
      destroyAfterEntrySpotJoin: actor.destroyAfterEntrySpotJoin
    });
  }

  async transferIn(actorId: string, state: ZLinkMessage): Promise<PlayActor> {
    const restored = state.decode<PlayActorTransferState>();
    const actor = new PlayActor(actorId, restored.displayName, undefined, restored.level, restored.wins);
    actor.roomId = restored.roomId;
    if (restored.destroyAfterEntrySpotJoin) {
      actor.markForDestroyAfterRoomLeave();
    }
    return actor;
  }
}

export { PlayActorTransferAdapter };
