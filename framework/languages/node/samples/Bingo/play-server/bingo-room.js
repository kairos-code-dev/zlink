const { BingoCard } = require('../shared/bingo-card');

class BingoRoomSpot {
  constructor(context) {
    this.context = context;
    this.players = new Map();
    this.draws = [];
    this.winners = [];
    this.status = 'WaitingForPlayers';
    this.requiredPlayers = 4;
    this.tickPromise = new Promise((resolve) => {
      this.resolveTick = resolve;
    });
  }

  join(playerId, numbers, boundSession) {
    if (this.status !== 'WaitingForPlayers') {
      throw new Error('Bingo room cannot accept players after the game starts.');
    }
    if (this.players.size >= this.requiredPlayers) {
      throw new Error('Bingo room is already full.');
    }

    const seat = this.players.size;
    this.players.set(playerId, {
      card: new BingoCard(numbers),
      boundSession,
      seat
    });

    return this.snapshot();
  }

  async start(actorId, draws) {
    if (this.players.size === 0 || this.hostActorId() !== actorId) {
      throw new Error('Only the host actor can start the bingo room.');
    }
    if (this.players.size !== this.requiredPlayers) {
      throw new Error(`Bingo room requires exactly ${this.requiredPlayers} players before start.`);
    }

    this.status = 'Running';
    this.draws = draws;
    await this.broadcast('BingoGameStarted', { state: this.snapshot() });
    this.timer = await this.context.addTimer('draw', 1, BingoDrawTimer, {
      stopOnUnhandledException: true
    });
    await this.tickPromise;
    await this.timer.cancel();
    return this.snapshot();
  }

  async drawNext() {
    const number = this.draws.shift();
    if (number === undefined || this.status !== 'Running') {
      this.resolveTick?.();
      return;
    }

    for (const [playerId, entry] of this.players) {
      const wasComplete = entry.card.isComplete();
      const isComplete = entry.card.mark(number);
      if (!wasComplete && isComplete) {
        this.winners.push(playerId);
      }
    }

    await this.broadcast('BingoNumberDrawn', {
      number,
      state: this.snapshot()
    });

    if (this.winners.length > 0) {
      this.status = 'Finished';
      await this.broadcast('BingoGameEnded', { state: this.snapshot() });
    }

    this.resolveTick?.();
  }

  async broadcast(packetName, payload) {
    await Promise.all([...this.players.entries()].map(([, entry]) =>
      entry.boundSession.send(payload).packetName(packetName).submit()
    ));
  }

  hostActorId() {
    return this.players.keys().next().value ?? '';
  }

  snapshot() {
    const hostActorId = this.hostActorId();
    return {
      status: this.status,
      hostActorId,
      canStart: this.status === 'WaitingForPlayers' && this.players.size === this.requiredPlayers,
      players: [...this.players.entries()].map(([actorId, entry]) => ({
        actorId,
        seat: entry.seat,
        isHost: actorId === hostActorId,
        numbers: entry.card.numbers,
        marks: entry.card.marks()
      })),
      winners: [...this.winners]
    };
  }
}

class BingoDrawTimer {
  async handle(room) {
    await room.drawNext();
  }
}

module.exports = { BingoRoomSpot };
