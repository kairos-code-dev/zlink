const { BingoCard } = require('./bingo-card');
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
    this.drawnNumbers = [];
    this.winners = [];
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
    if (player.card !== null) {
      throw new Error(`Actor '${actorId}' already submitted a Bingo card.`);
    }
    player.card = new BingoCard(cardNumbers);
  }

  canDraw(): boolean {
    return this.status === BingoRoomStatus.running
      && this.players.length === this.settings.requiredPlayers
      && this.players.every((player) => player.card !== null);
  }

  drawNext(): { number: number; drawSeq: number; finished: boolean } | null {
    if (!this.canDraw() || this.settings.drawDeck.length === 0) {
      return null;
    }
    const number = this.settings.drawDeck.shift();
    this.drawnNumbers.push(number);
    for (const player of this.players) {
      const lines = player.card.mark(number);
      if (lines > 0 && !this.winners.includes(player.actor.actorId)) {
        this.winners.push(player.actor.actorId);
      }
    }
    if (this.winners.length > 0) {
      this.status = BingoRoomStatus.finished;
    }
    return {
      number,
      drawSeq: this.drawnNumbers.length,
      finished: this.status === BingoRoomStatus.finished
    };
  }

  snapshot(): any {
    return {
      roomId: this.roomId,
      status: this.status,
      hostActorId: this.players[0]?.actor.actorId ?? null,
      canStart: false,
      drawSeq: this.drawnNumbers.length,
      lastDrawnNumber: this.drawnNumbers.at(-1) ?? null,
      drawnNumbers: [...this.drawnNumbers],
      players: this.players.map((player) => ({
        actorId: player.actor.actorId,
        displayName: player.actor.displayName,
        seat: player.seat,
        isHost: player.isHost,
        card: player.card === null ? [] : [...player.card.numbers],
        marks: player.card === null ? [] : [...player.card.marks],
        completedLines: player.card === null ? 0 : player.card.completedLines()
      })),
      winners: [...this.winners]
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
