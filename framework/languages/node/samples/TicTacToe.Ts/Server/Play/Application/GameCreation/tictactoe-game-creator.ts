const { TicTacToeMatch } = require('../../Domain/TicTacToe/tictactoe-match');
import type { TicTacToeMatch as TicTacToeMatchType } from '../../Domain/TicTacToe/tictactoe-match';

type TicTacToeRoom = {
  roomId: string;
  gameName: string;
  playEndpoint: string;
  match: TicTacToeMatchType;
  timerRegistered: boolean;
};

class TicTacToeGameCreator {
  private nextId: number;
  private readonly games: Map<string, TicTacToeRoom>;

  constructor() {
    this.nextId = 0;
    this.games = new Map();
  }

  create(gameName: string, playEndpoint: string): TicTacToeRoom {
    this.nextId += 1;
    const roomId = `${gameName}-${this.nextId}`;
    const room = {
      roomId,
      gameName,
      playEndpoint,
      match: new TicTacToeMatch(roomId),
      timerRegistered: false
    };
    this.games.set(roomId, room);
    return room;
  }

  require(roomId: string): TicTacToeRoom {
    const room = this.games.get(roomId);
    if (room === undefined) {
      throw new Error(`Room '${roomId}' does not exist.`);
    }
    return room;
  }
}

export { TicTacToeGameCreator };
export type { TicTacToeRoom };
