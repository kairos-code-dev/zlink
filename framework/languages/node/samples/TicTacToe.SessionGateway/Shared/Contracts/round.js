class SessionGatewayRound {
  constructor(matchId, xActor, oActor) {
    this.matchId = matchId;
    this.players = {
      X: { actorId: xActor.actorId, actor: xActor },
      O: { actorId: oActor.actorId, actor: oActor }
    };
    this.board = Array(9).fill('.');
    this.nextMark = 'X';
    this.status = 'Running';
    this.winnerActorId = undefined;
  }

  async notifyJoined() {
    const state = this.snapshot();
    await Promise.all([
      this.players.X.actor.notifyOpponentJoined(this.matchId, this.players.O.actorId, 'O', state),
      this.players.O.actor.notifyOpponentJoined(this.matchId, this.players.X.actorId, 'X', state)
    ]);
  }

  async place(actorId, cell) {
    const mark = this.markForActor(actorId);
    if (this.board[cell] !== '.') {
      throw new Error(`Cell ${cell} is already occupied.`);
    }
    this.board[cell] = mark;
    if (this.hasWinningLine(mark)) {
      this.status = 'Won';
      this.winnerActorId = actorId;
    } else {
      this.nextMark = mark === 'X' ? 'O' : 'X';
    }
    const state = this.snapshot();
    await Promise.all([
      this.players.X.actor.notifyTurnChanged(this.matchId, this.players[this.nextMark]?.actorId ?? '', state),
      this.players.O.actor.notifyTurnChanged(this.matchId, this.players[this.nextMark]?.actorId ?? '', state)
    ]);
    if (this.status === 'Won') {
      await Promise.all([
        this.players.X.actor.notifyGameEnded(this.matchId, this.winnerActorId, false, state),
        this.players.O.actor.notifyGameEnded(this.matchId, this.winnerActorId, false, state)
      ]);
    }
    return state;
  }

  snapshot() {
    return {
      matchId: this.matchId,
      board: this.board.join(''),
      status: this.status,
      winnerActorId: this.winnerActorId,
      xActorId: this.players.X.actorId,
      oActorId: this.players.O.actorId
    };
  }

  markForActor(actorId) {
    if (actorId === this.players.X.actorId) {
      return 'X';
    }
    if (actorId === this.players.O.actorId) {
      return 'O';
    }
    throw new Error(`Actor ${actorId} is not in match ${this.matchId}.`);
  }

  hasWinningLine(mark) {
    return [
      [0, 1, 2],
      [3, 4, 5],
      [6, 7, 8],
      [0, 3, 6],
      [1, 4, 7],
      [2, 5, 8],
      [0, 4, 8],
      [2, 4, 6]
    ].some(([a, b, c]) => this.board[a] === mark && this.board[b] === mark && this.board[c] === mark);
  }
}

module.exports = { SessionGatewayRound };
