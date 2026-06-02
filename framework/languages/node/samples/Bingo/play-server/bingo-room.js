const { BingoCard } = require('../shared/bingo-card');

class BingoRoomSpot {
  constructor(context) {
    this.context = context;
    this.players = new Map();
    this.draws = [];
    this.winner = undefined;
    this.tickPromise = new Promise((resolve) => {
      this.resolveTick = resolve;
    });
  }

  join(playerId, numbers, boundSession) {
    this.players.set(playerId, {
      card: new BingoCard(numbers),
      boundSession
    });
  }

  async start(draws) {
    this.draws = draws;
    this.timer = await this.context.addTimer('draw', 1, BingoDrawTimer, {
      stopOnUnhandledException: true
    });
    await this.tickPromise;
    await this.timer.cancel();
    return this.winner;
  }

  async drawNext() {
    const number = this.draws.shift();
    for (const [playerId, entry] of this.players) {
      if (entry.card.mark(number) && this.winner === undefined) {
        this.winner = playerId;
        await entry.boundSession
          .send({ winner: playerId, number })
          .packetName('BingoWinner')
          .submit();
      }
    }
    this.resolveTick?.();
  }
}

class BingoDrawTimer {
  async handle(room) {
    await room.drawNext();
  }
}

module.exports = { BingoRoomSpot };
