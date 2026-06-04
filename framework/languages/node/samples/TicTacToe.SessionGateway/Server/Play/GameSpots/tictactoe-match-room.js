const { SessionGatewayRound } = require('../../../Shared/Contracts/round');

class TicTacToeMatchRoom {
  constructor(matchId, matchName) {
    this.matchId = matchId;
    this.matchName = matchName;
    this.players = new Map();
    this.round = null;
  }

  async join(actor) {
    if (!this.players.has(actor.actorId)) {
      this.players.set(actor.actorId, actor);
    }
    if (this.players.size === 2 && this.round === null) {
      const [xActor, oActor] = [...this.players.values()];
      this.round = new SessionGatewayRound(this.matchId, xActor, oActor);
      await this.round.notifyJoined();
    }
    return this.state();
  }

  async place(actorId, cell) {
    if (this.round === null) {
      throw new Error(`Match '${this.matchId}' is waiting for players.`);
    }
    return await this.round.place(actorId, cell);
  }

  state() {
    return this.round?.snapshot() ?? {
      matchId: this.matchId,
      board: '.........',
      status: 'WaitingForPlayers',
      winnerActorId: undefined,
      xActorId: this.players.values().next().value?.actorId,
      oActorId: undefined
    };
  }
}

module.exports = { TicTacToeMatchRoom };
