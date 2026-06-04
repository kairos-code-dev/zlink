const { TicTacToeMatchRoom } = require('./tictactoe-match-room');

class TicTacToeMatchDirectory {
  constructor() {
    this.nextId = 0;
    this.rooms = new Map();
  }

  create(matchName) {
    this.nextId += 1;
    const matchId = `${matchName}-${this.nextId}`;
    const room = new TicTacToeMatchRoom(matchId, matchName);
    this.rooms.set(matchId, room);
    return room;
  }

  require(matchId) {
    const room = this.rooms.get(matchId);
    if (room === undefined) {
      throw new Error(`Match '${matchId}' does not exist.`);
    }
    return room;
  }
}

module.exports = { TicTacToeMatchDirectory };
