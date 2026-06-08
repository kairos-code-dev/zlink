const { TicTacToeMatch } = require('../../Domain/TicTacToe/tictactoe-game-models');

class TicTacToeGameDirectory {
  [key: string]: any;
  constructor() {
    this.nextId = 0;
    this.games = new Map();
  }

  create(gameName: string, playEndpoint: string): any {
    this.nextId += 1;
    const roomId = `${gameName}-${this.nextId}`;
    const room = new TicTacToeMatch(roomId, gameName, playEndpoint);
    this.games.set(roomId, room);
    return room;
  }

  require(roomId: string): any {
    const room = this.games.get(roomId);
    if (room === undefined) {
      throw new Error(`Room '${roomId}' does not exist.`);
    }
    return room;
  }
}

export { TicTacToeGameDirectory };
