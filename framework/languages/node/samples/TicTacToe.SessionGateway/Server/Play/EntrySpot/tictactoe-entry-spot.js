const { Inject } = require('@nestjs/common');
const { JoinMatchHandler } = require('./Handlers/join-match-handler');

class TicTacToeEntrySpot {
  constructor(joinMatch) {
    this.joinMatch = joinMatch;
  }

  handle(request) {
    return this.joinMatch.handle(request);
  }
}

Inject(JoinMatchHandler)(TicTacToeEntrySpot, undefined, 0);

module.exports = { TicTacToeEntrySpot };
