import type {
  ZLinkActor,
  ZLinkActorContext
} from '../../../../../../../packages/framework/dist';
import type { TicTacToeActor } from '../../../../../Shared/Contracts/messages';

type PlayClient = {
  send(message: unknown): {
    packetName(packetName: string): {
      metadata(key: string, value: string): {
        submit(signal?: AbortSignal): Promise<void>;
      };
    };
  };
};

class PlayActor implements ZLinkActor, TicTacToeActor {
  readonly actorId: string;
  readonly context: ZLinkActorContext;
  displayName: string;
  roomId?: string;
  destroyAfterEntrySpotJoin: boolean;
  disconnected: boolean;
  private nextSeq: number;
  private client: PlayClient | null;

  constructor(actorId: string, displayName: string, context: ZLinkActorContext) {
    this.actorId = actorId;
    this.context = context;
    this.displayName = displayName;
    this.destroyAfterEntrySpotJoin = false;
    this.disconnected = false;
    this.nextSeq = 0;
    this.client = null;
  }

  attachClient(client: PlayClient): void {
    this.client = client;
    this.disconnected = false;
  }

  detachClient(client: PlayClient): void {
    if (this.client === client) {
      this.client = null;
    }
  }

  markDisconnected(): void {
    this.client = null;
    this.disconnected = true;
  }

  markForDestroyAfterRoomLeave(): void {
    this.destroyAfterEntrySpotJoin = true;
  }

  async push(packetName: string, payload: unknown): Promise<void> {
    if (this.client === null) {
      return;
    }
    this.nextSeq += 1;
    await this.client
      .send(payload)
      .packetName(packetName)
      .metadata('seq', String(this.nextSeq))
      .submit();
  }
}

export { PlayActor };
