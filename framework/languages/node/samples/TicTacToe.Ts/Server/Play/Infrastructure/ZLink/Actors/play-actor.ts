import type {
  ZLinkActor,
  ZLinkActorContext
} from '@zlink-systems/framework';
import type { TicTacToeActor } from '../../../../../Shared/Contracts/messages';
import type { GameStateNotify, PlayerJoinedNotify, WinMilestoneNotify } from '../../../../../Shared/Contracts/messages';

type PlayClient = {
  send(message: unknown): {
    metadata(key: string, value: string): {
      submit(): Promise<void>;
    };
  };
};

class PlayActor implements ZLinkActor, TicTacToeActor {
  readonly actorId: string;
  readonly context!: ZLinkActorContext;
  displayName: string;
  level: number;
  wins: number;
  roomId?: string;
  destroyAfterEntrySpotJoin: boolean;
  disconnected: boolean;
  private nextSeq: number;
  private client: PlayClient | undefined;

  constructor(actorId: string, displayName: string, context?: ZLinkActorContext, level = 0, wins = 0) {
    this.actorId = actorId;
    if (context !== undefined) {
      Object.defineProperty(this, 'context', {
        configurable: true,
        enumerable: true,
        value: context
      });
    }
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

  async push(payload: PlayerJoinedNotify | GameStateNotify | WinMilestoneNotify): Promise<void> {
    this.nextSeq += 1;
    await this.context.boundSession
      .send(payload)
      .metadata('seq', String(this.nextSeq))
      .submit();
  }
}

export { PlayActor };
