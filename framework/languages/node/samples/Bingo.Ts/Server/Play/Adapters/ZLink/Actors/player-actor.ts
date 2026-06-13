import type { BingoActorRef } from '../../../../../Shared/Contracts/messages';
import type {
  ZLinkActor,
  ZLinkActorContext
} from '../../../../../../../packages/framework/dist';

class PlayerActor implements ZLinkActor {
  readonly context!: ZLinkActorContext;

  constructor(
    readonly actorId: string,
    public displayName: string,
    readonly actorRef: BingoActorRef,
    public destroyAfterEntrySpotJoin = false,
    public disconnected = false
  ) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.actorRef = actorRef;
  }

  markForDestroyAfterRoomLeave(): void {
    this.destroyAfterEntrySpotJoin = true;
  }

  markDisconnected(): void {
    this.disconnected = true;
  }
}

export { PlayerActor };
