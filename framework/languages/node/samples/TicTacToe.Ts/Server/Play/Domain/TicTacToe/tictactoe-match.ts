const { TicTacToeBoard } = require('./tictactoe-board');
import type { GameState, TicTacToeActor } from '../../../../Shared/Contracts/messages';

type JoinedPlayer = {
  actorId: string;
  mark: string;
  actor: TicTacToeActor;
};

type TicTacToeJoinChange = {
  state: GameState;
  joined: JoinedPlayer;
  newlyJoined: boolean;
};

type TicTacToeMoveChange = {
  state: GameState;
};

type TicTacToeTickChange = {
  state: GameState;
  changed: boolean;
};

class TicTacToeMatch {
  [key: string]: any;
  constructor(roomId: string, turnTimeoutMs: number = 15000) {
    this.roomId = roomId;
    this.turnTimeoutMs = turnTimeoutMs;
    this.players = new Map();
    this.board = new TicTacToeBoard();
    this.status = 'WaitingForPlayers';
    this.winner = null;
    this.nextTurn = null;
    this.lastMoveActorId = null;
    this.lastMoveCell = null;
    this.turnDeadline = null;
  }

  joinPlayer(actor: TicTacToeActor): TicTacToeJoinChange {
    const existing = this.players.get(actor.actorId);
    if (existing !== undefined) {
      return { state: this.snapshot(), joined: existing, newlyJoined: false };
    }
    if (this.players.size >= 2) {
      throw new Error(`Room '${this.roomId}' is full.`);
    }
    const mark = this.players.size === 0 ? 'X' : 'O';
    const joined = { actorId: actor.actorId, mark, actor };
    this.players.set(actor.actorId, joined);
    if (this.players.size === 2) {
      this.status = 'InProgress';
      this.nextTurn = this.xActorId();
      this.resetTurnDeadline();
    }
    return { state: this.snapshot(), joined, newlyJoined: true };
  }

  placeMark(actorId: string, cell: number): TicTacToeMoveChange {
    const player = this.players.get(actorId);
    if (player === undefined) {
      throw new Error(`Actor '${actorId}' has not joined room '${this.roomId}'.`);
    }
    if (this.status !== 'InProgress') {
      throw new Error(`Room '${this.roomId}' is not in progress.`);
    }
    if (this.nextTurn !== actorId) {
      throw new Error(`Actor '${actorId}' cannot move out of turn.`);
    }
    this.board.place(player.mark, cell);
    this.lastMoveActorId = actorId;
    this.lastMoveCell = cell;
    this.advanceAfterMove(actorId, player.mark);
    return { state: this.snapshot() };
  }

  tick(now: number = Date.now()): TicTacToeTickChange {
    if (this.status !== 'InProgress' || this.turnDeadline === null || now < this.turnDeadline) {
      return { state: this.snapshot(), changed: false };
    }
    const timedOut = this.nextTurn;
    const winner = [...this.players.values()]
      .find((player) => player.actorId !== timedOut)?.actorId ?? null;
    this.status = 'TurnTimedOut';
    this.winner = winner;
    this.nextTurn = null;
    this.lastMoveActorId = timedOut;
    this.lastMoveCell = null;
    this.turnDeadline = null;
    return { state: this.snapshot(), changed: true };
  }

  snapshot(): GameState {
    return {
      roomId: this.roomId,
      board: this.board.snapshot(),
      status: this.status,
      winner: this.winner,
      nextTurn: this.nextTurn,
      xActorId: this.xActorId(),
      oActorId: this.oActorId(),
      lastMoveActorId: this.lastMoveActorId,
      lastMoveCell: this.lastMoveCell
    };
  }

  xActorId(): string | null {
    return [...this.players.values()].find((player) => player.mark === 'X')?.actorId ?? null;
  }

  oActorId(): string | null {
    return [...this.players.values()].find((player) => player.mark === 'O')?.actorId ?? null;
  }

  advanceAfterMove(actorId: string, mark: string): void {
    if (this.board.hasWon(mark)) {
      this.status = 'Won';
      this.winner = actorId;
      this.nextTurn = null;
      this.turnDeadline = null;
      return;
    }
    if (this.board.isFull()) {
      this.status = 'Draw';
      this.winner = null;
      this.nextTurn = null;
      this.turnDeadline = null;
      return;
    }
    this.nextTurn = [...this.players.values()].find((player) => player.actorId !== actorId).actorId;
    this.resetTurnDeadline();
  }

  resetTurnDeadline(): void {
    this.turnDeadline = Date.now() + this.turnTimeoutMs;
  }
}

export { TicTacToeMatch };
export type { JoinedPlayer, TicTacToeJoinChange, TicTacToeMoveChange, TicTacToeTickChange };
