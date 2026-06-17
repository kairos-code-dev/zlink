import { Injectable } from '@nestjs/common';
import { BingoRoomStatus, numberDrawnNotify, playerJoinedNotify, stateEnvelope, submitBingoCardRes } from '../../../../../Shared/Contracts/messages';
import { BingoRoomGame } from '../../../Domain/Bingo/bingo-room-game';
import { createRoomSettings } from '../../../Domain/Bingo/bingo-room-models';
import { BingoNotificationPublisher } from '../Notifications/bingo-notification-publisher';
import type {
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext
} from '@zlink-systems/framework';
import type { PlayerActor as PlayerActorType } from '../Actors/player-actor';
import type {
  BingoRoomSettings,
  BingoRoomGame as BingoRoomGameType,
  BingoRoomSnapshot
} from '../../../Domain/Bingo/bingo-room-game';
import type {
  SubmitBingoCardReq,
  SubmitBingoCardRes
} from '../../../../../Shared/Contracts/messages';

type BingoActor = {
  actorId: string;
  displayName: string;
};

type BingoNotificationPublisherLike = Pick<
  BingoNotificationPublisher,
  'publish' | 'playerJoined' | 'gameStarted' | 'numberDrawn' | 'gameEnded'
>;

@Injectable()
class BingoRoomSpot implements ZLinkSpot<PlayerActorType> {
  private static notifications: BingoNotificationPublisherLike | null = null;
  readonly context!: ZLinkSpotContext<PlayerActorType, BingoRoomSpot>;
  private roomId: string;
  private game: BingoRoomGameType;
  private cleanupStarted = false;

  constructor() {
    this.roomId = 'bingo-room';
    this.game = new BingoRoomGame(this.roomId, createRoomSettings(0));
  }

  static useNotifications(notifications: BingoNotificationPublisherLike): void {
    BingoRoomSpot.notifications = notifications;
  }

  async onInitialize(): Promise<void> {
    this.roomId = this.context.spotRid;
  }

  async onActorJoin(actor: PlayerActorType): Promise<ZLinkSpotActorJoinResponse> {
    try {
      const joined = this.game.join(actor);
      const state = this.snapshot();
      if (joined.joined) {
        const playerJoinedEvents = this.game.players
          .filter((entry) => entry.actor.actorId !== actor.actorId)
          .map((entry) =>
            this.requireNotifications().playerJoined(entry.actor, playerJoinedNotify(this.roomId, actor, joined.player.seat, joined.player.isHost, state))
          );
        if (playerJoinedEvents.length > 0) {
          await this.requireNotifications().publish(playerJoinedEvents);
        }
      }
      if (joined.started) {
        await this.requireNotifications().publish(this.game.players.map((player) =>
          this.requireNotifications().gameStarted(player.actor, stateEnvelope(this.snapshot()))
        ));
      }
      return { accepted: true };
    } catch (error) {
      return {
        accepted: false
      };
    }
  }

  initializeRoom(settings: BingoRoomSettings): void {
    this.game = new BingoRoomGame(this.roomId, settings);
  }

  async onJoinActor(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onLeaveActor(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onDisconnectActor(actor: PlayerActorType): Promise<void> {
    actor.markDisconnected();
  }

  async submitCard(actor: PlayerActorType | BingoActor, request: SubmitBingoCardReq): Promise<SubmitBingoCardRes> {
    this.game.submitCard(actor.actorId, request.card);
    return submitBingoCardRes(this.snapshot());
  }

  async drawNextNumber(): Promise<BingoRoomSnapshot> {
    const drawn = this.game.drawNext();
    if (drawn === null) {
      return this.snapshot();
    }
    const state = this.snapshot();
    await this.requireNotifications().publish(this.game.players.map((player) =>
      this.requireNotifications().numberDrawn(player.actor, numberDrawnNotify(this.roomId, drawn.drawSeq, drawn.number, state))
    ));
    if (drawn.finished) {
      await this.requireNotifications().publish(this.game.players.map((player) =>
        this.requireNotifications().gameEnded(player.actor, stateEnvelope(state))
      ));
      await this.leaveFinishedActors();
    }
    return state;
  }

  private async leaveFinishedActors(): Promise<void> {
    if (this.cleanupStarted || this.snapshot().status !== BingoRoomStatus.Finished) {
      return;
    }
    this.cleanupStarted = true;
    for (const player of [...this.game.players]) {
      const actor = player.actor as PlayerActorType;
      actor.markForDestroyAfterRoomLeave();
      await this.context.leaveActor(actor);
    }
  }

  snapshot(): BingoRoomSnapshot {
    return this.game.snapshot();
  }

  private requireNotifications(): BingoNotificationPublisherLike {
    if (BingoRoomSpot.notifications === null) {
      throw new Error('BingoRoomSpot notifications were not configured.');
    }
    return BingoRoomSpot.notifications;
  }
}

export { BingoRoomSpot };
export type { BingoActor };
