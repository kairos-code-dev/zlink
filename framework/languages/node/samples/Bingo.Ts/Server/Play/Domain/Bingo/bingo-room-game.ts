const { BingoGame } = require('./bingo-game');
const { BingoRoomStatus } = require('./bingo-room-models');

type BingoActor = {
  actorId: string;
  displayName: string;
};

type BingoRoomSettings = {
  requiredPlayers: number;
  drawDeck: number[];
};

type BingoPlayerSeat = {
  actor: BingoActor;
  seat: number;
  card: any | null;
  isHost: boolean;
};

class BingoRoomGame {
  [key: string]: any;
  constructor(roomId: string, settings: BingoRoomSettings) {
    this.roomId = roomId;
    this.settings = {
      requiredPlayers: settings.requiredPlayers,
      drawDeck: [...settings.drawDeck]
    };
    this.status = BingoRoomStatus.waitingForPlayers;
    this.players = [];
    this.game = new BingoGame(this.settings.drawDeck);
  }

  join(actor: BingoActor): { joined: boolean; player: BingoPlayerSeat; started: boolean } {
    const existing = this.players.find((player) => player.actor.actorId === actor.actorId);
    if (existing !== undefined) {
      return { joined: false, player: existing, started: false };
    }
    if (this.status !== BingoRoomStatus.waitingForPlayers || this.players.length >= this.settings.requiredPlayers) {
      throw new Error(`Room ${this.roomId} cannot accept more players.`);
    }
    const player = {
      actor,
      seat: this.players.length,
      card: null,
      isHost: this.players.length === 0
    };
    this.players.push(player);
    let started = false;
    if (this.players.length === this.settings.requiredPlayers) {
      this.status = BingoRoomStatus.running;
      started = true;
    }
    return { joined: true, player, started };
  }

  submitCard(actorId: string, cardNumbers: number[]): void {
    if (this.status !== BingoRoomStatus.running) {
      throw new Error(`Room ${this.roomId} is not running.`);
    }
    const player = this.requirePlayer(actorId);
    player.card = this.game.submitCard({ actorId, card: player.card }, cardNumbers);
  }

  canDraw(): boolean {
    return this.status === BingoRoomStatus.running
      && this.game.canDraw(this.players.map((player) => ({ actorId: player.actor.actorId, card: player.card })), this.settings.requiredPlayers);
  }

  drawNext(): { number: number; drawSeq: number; finished: boolean } | null {
    if (!this.canDraw()) {
      return null;
    }
    const drawn = this.game.drawNext(this.players.map((player) => ({ actorId: player.actor.actorId, card: player.card })));
    if (drawn !== null && drawn.finished) {
      this.status = BingoRoomStatus.finished;
    }
    return drawn;
  }

  snapshot(): any {
    return {
      roomId: this.roomId,
      status: this.status,
      hostActorId: this.players[0]?.actor.actorId ?? null,
      canStart: false,
      drawSeq: this.game.drawnNumbers.length,
      lastDrawnNumber: this.game.lastDrawnNumber(),
      drawnNumbers: [...this.game.drawnNumbers],
      players: this.players.map((player) => ({
        actorId: player.actor.actorId,
        displayName: player.actor.displayName,
        seat: player.seat,
        isHost: player.isHost,
        card: player.card === null ? [] : [...player.card.numbers],
        marks: player.card === null ? [] : [...player.card.marks],
        completedLines: player.card === null ? 0 : player.card.completedLines()
      })),
      winners: [...this.game.winners]
    };
  }

  requirePlayer(actorId: string): BingoPlayerSeat {
    const player = this.players.find((entry) => entry.actor.actorId === actorId);
    if (player === undefined) {
      throw new Error(`Actor '${actorId}' has not joined room '${this.roomId}'.`);
    }
    return player;
  }
}

export { BingoRoomGame };
