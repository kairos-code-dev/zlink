const {
  createJsonMessage,
  numberDrawnNotify,
  playerJoinedNotify,
  readJsonMessage,
  roomJoinError,
  stateEnvelope,
  submitBingoCardRes
} = require('../../../../../Shared/Contracts/messages');
const { BingoRoomGame } = require('../../../Domain/Bingo/bingo-room-game');
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

class BingoRoomSpot {
  [key: string]: any;
  constructor(roomId: string, settings: BingoRoomSettings, notifications: any) {
    this.roomId = roomId;
    this.notifications = notifications;
    this.game = new BingoRoomGame(roomId, settings);
  }

  async onActorJoin(actor: BingoActor, request: any): Promise<{ accepted: boolean; reply: any }> {
    const admission = readJsonMessage(request) as BingoRoomJoinReq;
    actor.displayName = admission.displayName ?? actor.displayName;
    try {
      const joined = this.game.join(actor);
      const state = this.snapshot();
      if (joined.joined) {
        await this.notifications.publish(this.game.players.map((entry) =>
          this.notifications.playerJoined(entry.actor, playerJoinedNotify(this.roomId, actor, joined.player.seat, joined.player.isHost, state))
        ));
      }
      if (joined.started) {
        await this.notifications.publish(this.game.players.map((player) =>
          this.notifications.gameStarted(player.actor, stateEnvelope(this.snapshot()))
        ));
      }
      return { accepted: true, reply: createJsonMessage(stateEnvelope(this.snapshot())) };
    } catch (error) {
      return {
        accepted: false,
        reply: createJsonMessage(roomJoinError(error instanceof Error ? error.message : String(error)))
      };
    }
  }

  async submitCard(actor: BingoActor, request: SubmitBingoCardReq): Promise<SubmitBingoCardRes> {
    this.game.submitCard(actor.actorId, request.card);
    return submitBingoCardRes(this.snapshot());
  }

  async runTimerDraws(): Promise<any> {
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

  snapshot(): any {
    return this.game.snapshot();
  }
}

export { BingoRoomSpot };
