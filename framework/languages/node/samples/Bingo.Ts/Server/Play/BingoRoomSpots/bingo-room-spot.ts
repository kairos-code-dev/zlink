const { deterministicCard } = require('../../../Shared/Contracts/messages');
const { BingoCard } = require('./bingo-card');
const { BingoRoomStatus } = require('./bingo-room-models');

class BingoRoomSpot {
  [key: string]: any;
  constructor(roomId, settings, notifications) {
    this.roomId = roomId;
    this.settings = settings;
    this.notifications = notifications;
    this.status = BingoRoomStatus.waitingForPlayers;
    this.players = [];
    this.cards = new Map();
    this.drawnNumbers = [];
    this.winners = [];
  }

  async join(actor) {
    if (this.players.some((player) => player.actor.actorId === actor.actorId)) {
      return { state: this.snapshot() };
    }
    if (this.status !== BingoRoomStatus.waitingForPlayers || this.players.length >= this.settings.requiredPlayers) {
      throw new Error(`Room ${this.roomId} cannot accept more players.`);
    }

    const seat = this.players.length;
    const card = new BingoCard(deterministicCard(actor.actorId));
    const player = { actor, seat, card, isHost: seat === 0 };
    this.players.push(player);
    this.cards.set(actor.actorId, card);
    const state = this.snapshot();
    await this.notifications.publish(this.players.map((entry) =>
      this.notifications.playerJoined(entry.actor, {
        roomId: this.roomId,
        actorId: actor.actorId,
        displayName: actor.displayName,
        seat,
        isHost: player.isHost,
        state
      })
    ));
    return { state };
  }

  async start(actor) {
    if (!this.players.some((player) => player.actor.actorId === actor.actorId && player.isHost)) {
      throw new Error('Only the room host can start Bingo.');
    }
    if (this.players.length !== this.settings.requiredPlayers) {
      throw new Error('Bingo requires four players before start.');
    }
    if (this.status === BingoRoomStatus.waitingForPlayers) {
      this.status = BingoRoomStatus.running;
      const state = this.snapshot();
      await this.notifications.publish(this.players.map((player) =>
        this.notifications.gameStarted(player.actor, { state })
      ));
    }
    return { state: this.snapshot() };
  }

  async runTimerDraws() {
    while (this.status === BingoRoomStatus.running && this.settings.drawDeck.length > 0) {
      const number = this.settings.drawDeck.shift();
      this.drawnNumbers.push(number);
      for (const player of this.players) {
        const lines = player.card.mark(number);
        if (lines > 0 && !this.winners.includes(player.actor.actorId)) {
          this.winners.push(player.actor.actorId);
        }
      }
      const state = this.snapshot();
      if (this.winners.length > 0) {
        this.status = BingoRoomStatus.finished;
        const ended = this.snapshot();
        await this.notifications.publish(this.players.map((player) =>
          this.notifications.gameEnded(player.actor, { state: ended })
        ));
        return ended;
      }
      await this.notifications.publish(this.players.map((player) =>
        this.notifications.numberDrawn(player.actor, {
          roomId: this.roomId,
          drawSeq: this.drawnNumbers.length,
          number,
          state
        })
      ));
    }
    return this.snapshot();
  }

  snapshot() {
    return {
      roomId: this.roomId,
      status: this.status,
      hostActorId: this.players[0]?.actor.actorId ?? null,
      canStart: this.status === BingoRoomStatus.waitingForPlayers && this.players.length === this.settings.requiredPlayers,
      drawSeq: this.drawnNumbers.length,
      lastDrawnNumber: this.drawnNumbers.at(-1) ?? null,
      drawnNumbers: [...this.drawnNumbers],
      players: this.players.map((player) => ({
        actorId: player.actor.actorId,
        displayName: player.actor.displayName,
        seat: player.seat,
        isHost: player.isHost,
        card: [...player.card.numbers],
        marks: [...player.card.marks],
        completedLines: player.card.completedLines()
      })),
      winners: [...this.winners]
    };
  }
}

export { BingoRoomSpot };
