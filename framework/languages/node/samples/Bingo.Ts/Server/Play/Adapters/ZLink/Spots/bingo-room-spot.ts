const {
  createProtobufMessage,
  numberDrawnNotify,
  playerJoinedNotify,
  readProtobufMessage,
  roomJoinError,
  stateEnvelope,
  PacketNames,
  submitBingoCardRes
} = require('../../../../../Shared/Contracts/messages');
const { BingoRoomGame } = require('../../../Domain/Bingo/bingo-room-game');
const { createRoomSettings } = require('../../../Domain/Bingo/bingo-room-models');
const { SubmitBingoCardHandler } = require('./Handlers/submit-bingo-card-handler');
const { PlayerActor } = require('../Actors/player-actor');
import type {
  Message,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotContext
} from '../../../../../../../packages/framework/dist';
import type { PlayerActor as PlayerActorType } from '../Actors/player-actor';
import type { BingoNotificationPublisher } from '../Notifications/bingo-notification-publisher';
import type {
  BingoRoomGame as BingoRoomGameType,
  BingoRoomSnapshot
} from '../../../Domain/Bingo/bingo-room-game';
import type {
  BingoRoomJoinReq,
  SubmitBingoCardReq,
  SubmitBingoCardRes,
  StateEnvelope
} from '../../../../../Shared/Contracts/messages';

type BingoActor = {
  actorId: string;
  displayName: string;
};

type BingoRoomSettings = {
  requiredPlayers: number;
  drawDeck: number[];
};

type BingoNotificationPublisherLike = Pick<
  BingoNotificationPublisher,
  'publish' | 'playerJoined' | 'gameStarted' | 'numberDrawn' | 'gameEnded'
>;

class BingoRoomSpot implements ZLinkSpot<PlayerActorType> {
  readonly context?: ZLinkSpotContext;
  readonly roomId: string;
  private readonly notifications: BingoNotificationPublisherLike;
  private readonly game: BingoRoomGameType;

  constructor(
    contextOrRoomId?: ZLinkSpotContext | string,
    settings?: BingoRoomSettings,
    notifications?: BingoNotificationPublisherLike
  ) {
    if (typeof contextOrRoomId === 'object') {
      this.context = contextOrRoomId;
      this.roomId = contextOrRoomId.spotRid;
      this.notifications = notifications ?? createNoopNotificationPublisher();
      this.game = new BingoRoomGame(this.roomId, settings ?? createRoomSettings(undefined, 0));
      return;
    }

    this.roomId = contextOrRoomId ?? 'bingo-room';
    this.notifications = notifications ?? createNoopNotificationPublisher();
    this.game = new BingoRoomGame(this.roomId, settings ?? createRoomSettings(undefined, 0));
  }

  configure(): void {
    this.context?.handlers.addActorPacket(SubmitBingoCardHandler, PlayerActor, PacketNames.submitBingoCardReq);
  }

  async onActorJoin(actor: PlayerActorType, request: Message): Promise<ZLinkSpotActorJoinResponse> {
    const admission = readProtobufMessage(request) as BingoRoomJoinReq;
    actor.displayName = admission.displayName ?? actor.displayName;
    try {
      const joined = this.game.join(actor);
      const state = this.snapshot();
      if (joined.joined) {
        await this.notifications.publish(this.game.players
          .filter((entry) => entry.actor.actorId !== actor.actorId)
          .map((entry) =>
            this.notifications.playerJoined(entry.actor, playerJoinedNotify(this.roomId, actor, joined.player.seat, joined.player.isHost, state))
          ));
      }
      if (joined.started) {
        await this.notifications.publish(this.game.players.map((player) =>
          this.notifications.gameStarted(player.actor, stateEnvelope(this.snapshot()))
        ));
      }
      return { accepted: true, reply: createProtobufMessage(stateEnvelope(this.snapshot())) };
    } catch (error) {
      return {
        accepted: false,
        reply: createProtobufMessage(roomJoinError(error instanceof Error ? error.message : String(error)))
      };
    }
  }

  async onPostActorJoined(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onActorLeft(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async onActorDisconnected(actor: PlayerActorType): Promise<void> {
    void actor;
  }

  async submitCard(actor: PlayerActorType | BingoActor, request: SubmitBingoCardReq): Promise<SubmitBingoCardRes> {
    this.game.submitCard(actor.actorId, request.card);
    return submitBingoCardRes(this.snapshot());
  }

  async runTimerDraws(): Promise<BingoRoomSnapshot> {
    while (this.game.canDraw()) {
      const drawn = this.game.drawNext();
      if (drawn === null) {
        return this.snapshot();
      }
      const state = this.snapshot();
      await this.notifications.publish(this.game.players.map((player) =>
        this.notifications.numberDrawn(player.actor, numberDrawnNotify(this.roomId, drawn.drawSeq, drawn.number, state))
      ));
      if (drawn.finished) {
        await this.notifications.publish(this.game.players.map((player) =>
          this.notifications.gameEnded(player.actor, stateEnvelope(state))
        ));
        return state;
      }
    }
    return this.snapshot();
  }

  snapshot(): BingoRoomSnapshot {
    return this.game.snapshot();
  }
}

function createNoopNotificationPublisher(): BingoNotificationPublisherLike {
  return {
    async publish(): Promise<void> {},
    playerJoined(actor, payload) {
      return { actor, packetName: PacketNames.playerJoinedNotify, payload };
    },
    gameStarted(actor, payload) {
      return { actor, packetName: PacketNames.gameStartedNotify, payload };
    },
    numberDrawn(actor, payload) {
      return { actor, packetName: PacketNames.numberDrawnNotify, payload };
    },
    gameEnded(actor, payload) {
      return { actor, packetName: PacketNames.gameEndedNotify, payload };
    }
  };
}

export { BingoRoomSpot };
export type { BingoActor, BingoRoomSettings };
