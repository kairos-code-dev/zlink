import type {
  ZLinkActor,
  ZLinkActorContext
} from '@zlink-systems/framework';
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
  level: number;
  wins: number;
  roomId?: string;
  destroyAfterEntrySpotJoin: boolean;
  disconnected: boolean;
  private nextSeq: number;
  private client: PlayClient | undefined;

  constructor(actorId: string, displayName: string, context: ZLinkActorContext, level = 0, wins = 0) {
    this.actorId = actorId;
    this.context = context;
    this.displayName = displayName;
    this.level = level;
    this.wins = wins;
    this.destroyAfterEntrySpotJoin = false;
    this.disconnected = false;
    this.nextSeq = 0;
    this.client = undefined;
  }

  attachClient(client: PlayClient): void {
    this.client = client;
    this.disconnected = false;
  }

  detachClient(client: PlayClient): void {
    if (this.client === client) {
      this.client = undefined;
    }
  }

  markDisconnected(): void {
    this.client = undefined;
    this.disconnected = true;
  }

  markForDestroyAfterRoomLeave(): void {
    this.destroyAfterEntrySpotJoin = true;
  }

  async push(packetName: string, payload: unknown): Promise<void> {
    this.nextSeq += 1;
    await this.context.boundSession
      .send(payload)
      .packetName(packetName)
      .metadata('seq', String(this.nextSeq))
      .submit();
  }
}

export { PlayActor };
