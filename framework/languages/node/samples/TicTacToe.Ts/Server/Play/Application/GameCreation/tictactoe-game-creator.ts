const { TicTacToeMatch } = require('../../Domain/TicTacToe/tictactoe-match');
import type { TicTacToeMatch as TicTacToeMatchType } from '../../Domain/TicTacToe/tictactoe-match';

type TicTacToeRoom = {
  roomId: string;
  gameName: string;
  playEndpoint: string;
  match: TicTacToeMatchType;
  timerRegistered: boolean;
  cleanupStarted: boolean;
};

type FinishedRoomCleaner = (room: TicTacToeRoom) => Promise<void>;

class TicTacToeGameCreator {
  private nextId: number;
  private readonly games: Map<string, TicTacToeRoom>;
  private finishedRoomCleaner: FinishedRoomCleaner | null;

  constructor() {
    this.nextId = 0;
    this.games = new Map();
    this.finishedRoomCleaner = null;
  }

  create(gameName: string, playEndpoint: string): TicTacToeRoom {
    this.nextId += 1;
    const roomId = `${gameName}-${this.nextId}`;
    const room = {
      roomId,
      gameName,
      playEndpoint,
      match: new TicTacToeMatch(roomId),
      timerRegistered: false,
      cleanupStarted: false
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

  setFinishedRoomCleaner(cleaner: FinishedRoomCleaner): void {
    this.finishedRoomCleaner = cleaner;
  }

  async cleanupFinishedRoom(room: TicTacToeRoom): Promise<void> {
    if (room.cleanupStarted || !isTerminal(room.match.snapshot().status)) {
      return;
    }
    room.cleanupStarted = true;
    if (this.finishedRoomCleaner === null) {
      throw new Error('TicTacToe finished room cleaner has not been registered.');
    }
    await this.finishedRoomCleaner(room);
  }
}

export { TicTacToeGameCreator };
export type { TicTacToeRoom };

function isTerminal(status: string): boolean {
  return status === 'Won' || status === 'Draw' || status === 'TurnTimedOut';
}
