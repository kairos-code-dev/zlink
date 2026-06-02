class TicTacToeBoard {
  constructor() {
    this.cells = Array(9).fill(null);
  }

  place(playerId, mark, cell) {
    if (this.cells[cell] !== null) {
      throw new Error(`Cell ${cell} is already occupied.`);
    }
    this.cells[cell] = mark;
    return this.winner() ?? null;
  }

  winner() {
    const lines = [
      [0, 1, 2],
      [3, 4, 5],
      [6, 7, 8],
      [0, 3, 6],
      [1, 4, 7],
      [2, 5, 8],
      [0, 4, 8],
      [2, 4, 6]
    ];
    for (const [a, b, c] of lines) {
      if (this.cells[a] !== null && this.cells[a] === this.cells[b] && this.cells[a] === this.cells[c]) {
        return this.cells[a];
      }
    }
    return undefined;
  }
}

module.exports = { TicTacToeBoard };
