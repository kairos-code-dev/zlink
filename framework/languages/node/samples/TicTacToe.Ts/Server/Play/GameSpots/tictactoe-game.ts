const { TicTacToeMatchRoom } = require('./tictactoe-game-models');

class TicTacToeGameDirectory {
  [key: string]: any;
  constructor() {
    this.nextId = 0;
    this.games = new Map();
  }

  create(gameName: string, playEndpoint: string): any {
    this.nextId += 1;
    const gameId = `${gameName}-${this.nextId}`;
    const room = new TicTacToeMatchRoom(gameId, gameName, playEndpoint);
    this.games.set(gameId, room);
    return room;
  }

  require(gameId: string): any {
    const room = this.games.get(gameId);
    if (room === undefined) {
      throw new Error(`Game '${gameId}' does not exist.`);
    }
    return room;
  }
}

export { TicTacToeGameDirectory };
