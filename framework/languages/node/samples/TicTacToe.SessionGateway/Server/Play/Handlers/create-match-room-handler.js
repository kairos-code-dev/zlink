const { Inject } = require('@nestjs/common');
const { TicTacToeMatchDirectory } = require('../GameSpots/tictactoe-match-directory');

class CreateMatchRoomHandler {
  constructor(matches) {
    this.matches = matches;
  }

  handle(request) {
    const room = this.matches.create(request.matchName ?? 'match');
    return {
      matchId: room.matchId,
      matchName: room.matchName,
      state: room.state()
    };
  }
}

Inject(TicTacToeMatchDirectory)(CreateMatchRoomHandler, undefined, 0);

module.exports = { CreateMatchRoomHandler };
