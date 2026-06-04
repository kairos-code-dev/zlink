class TicTacToeEntrySpot {
  constructor(joinMatch) {
    this.joinMatch = joinMatch;
  }

  handle(request) {
    return this.joinMatch.handle(request);
  }
}

module.exports = { TicTacToeEntrySpot };
