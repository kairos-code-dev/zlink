type TicTacToeActor = {
  actorId: string;
  displayName: string;
};

type JoinedPlayer = {
  actorId: string;
  mark: string;
  actor: TicTacToeActor;
};

class TicTacToeBoard {
  [key: string]: any;
  constructor() {
    this.cells = Array(9).fill('.');
  }

  place(cell: number, mark: string): void {
    if (!Number.isInteger(cell) || cell < 0 || cell > 8) {
      throw new Error(`Cell '${cell}' is outside the board.`);
    }
    if (this.cells[cell] !== '.') {
      throw new Error(`Cell '${cell}' is already occupied.`);
    }
    this.cells[cell] = mark;
  }

  winnerMark(): string | null {
    for (const [a, b, c] of winningLines()) {
      if (this.cells[a] !== '.' && this.cells[a] === this.cells[b] && this.cells[a] === this.cells[c]) {
        return this.cells[a];
      }
    }
    return null;
  }

  isFull(): boolean {
    return this.cells.every((cell) => cell !== '.');
  }

  toString(): string {
    return this.cells.join('');
  }
}

class TicTacToeMatch {
  [key: string]: any;
  constructor(roomId: string, gameName: string, playEndpoint: string) {
    this.roomId = roomId;
    this.gameName = gameName;
    this.playEndpoint = playEndpoint;
    this.players = new Map();
    this.board = new TicTacToeBoard();
    this.status = 'WaitingForPlayers';
    this.winner = null;
    this.nextTurn = null;
    this.lastMoveActorId = null;
    this.lastMoveCell = null;
    this.timerRegistered = false;
  }

  addPlayer(actor: TicTacToeActor): { joined: JoinedPlayer; newlyJoined: boolean; started: boolean } {
    const existing = this.players.get(actor.actorId);
    if (existing !== undefined) {
      return { joined: existing, newlyJoined: false, started: false };
    }
    if (this.players.size >= 2) {
      throw new Error(`Room '${this.roomId}' is full.`);
    }
    const mark = this.players.size === 0 ? 'X' : 'O';
    const joined = { actorId: actor.actorId, mark, actor };
    this.players.set(actor.actorId, joined);
    let started = false;
    if (this.players.size === 2) {
      this.status = 'Running';
      this.nextTurn = this.xActorId();
      started = true;
    }
    return { joined, newlyJoined: true, started };
  }

  place(actorId: string, cell: number): any {
    const player = this.players.get(actorId);
    if (player === undefined) {
      throw new Error(`Actor '${actorId}' has not joined room '${this.roomId}'.`);
    }
    if (this.status !== 'Running') {
      throw new Error(`Room '${this.roomId}' is not running.`);
    }
    if (this.nextTurn !== actorId) {
      throw new Error(`Actor '${actorId}' cannot move out of turn.`);
    }
    this.board.place(cell, player.mark);
    this.lastMoveActorId = actorId;
    this.lastMoveCell = cell;
    const winnerMark = this.board.winnerMark();
    if (winnerMark !== null) {
      this.status = 'Finished';
      this.winner = [...this.players.values()].find((joined) => joined.mark === winnerMark).actorId;
      this.nextTurn = null;
    } else if (this.board.isFull()) {
      this.status = 'Draw';
      this.nextTurn = null;
    } else {
      this.nextTurn = [...this.players.values()].find((joined) => joined.actorId !== actorId).actorId;
    }
    return this.state();
  }

  state(): any {
    return {
      roomId: this.roomId,
      board: this.board.toString(),
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
}

function winningLines(): number[][] {
  return [
    [0, 1, 2],
    [3, 4, 5],
    [6, 7, 8],
    [0, 3, 6],
    [1, 4, 7],
    [2, 5, 8],
    [0, 4, 8],
    [2, 4, 6]
  ];
}

export { TicTacToeBoard, TicTacToeMatch };
