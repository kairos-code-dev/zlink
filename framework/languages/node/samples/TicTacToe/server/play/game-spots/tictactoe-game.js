class TicTacToeGame {
  constructor(matchId) {
    this.matchId = matchId;
    this.cells = Array(9).fill('.');
    this.players = new Map();
  }

  join(actorId, mark) {
    this.players.set(actorId, mark);
  }

  place(actorId, cell) {
    const mark = this.players.get(actorId);
    if (mark === undefined) {
      throw new Error(`Actor ${actorId} is not joined to match ${this.matchId}.`);
    }
    if (this.cells[cell] !== '.') {
      throw new Error(`Cell ${cell} is already occupied.`);
    }
    this.cells[cell] = mark;
    const winnerMark = this.winnerMark();
    return winnerMark === undefined
      ? undefined
      : [...this.players.entries()].find(([, value]) => value === winnerMark)?.[0];
  }

  snapshot() {
    return this.cells.join('');
  }

  winnerMark() {
    return [
      [0, 1, 2],
      [3, 4, 5],
      [6, 7, 8],
      [0, 3, 6],
      [1, 4, 7],
      [2, 5, 8],
      [0, 4, 8],
      [2, 4, 6]
    ].find(([a, b, c]) => this.cells[a] !== '.' && this.cells[a] === this.cells[b] && this.cells[a] === this.cells[c])
      ? this.cells.find((cell, index) => {
        return [
          [0, 1, 2],
          [3, 4, 5],
          [6, 7, 8],
          [0, 3, 6],
          [1, 4, 7],
          [2, 5, 8],
          [0, 4, 8],
          [2, 4, 6]
        ].some(([a, b, c]) => index === a && cell !== '.' && cell === this.cells[b] && cell === this.cells[c]);
      })
      : undefined;
  }
}

module.exports = { TicTacToeGame };
