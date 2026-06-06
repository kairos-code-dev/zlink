const { createJsonMessage, readJsonMessage } = require('../../../Shared/Contracts/messages');

class TicTacToeMatchRoom {
  [key: string]: any;
  constructor(gameId, gameName, playEndpoint) {
    this.gameId = gameId;
    this.gameName = gameName;
    this.playEndpoint = playEndpoint;
    this.players = new Map();
    this.cells = Array(9).fill('.');
    this.status = 'WaitingForPlayers';
    this.winner = null;
    this.timerRegistered = false;
  }

  addPlayer(actor) {
    if (!this.players.has(actor.actorId)) {
      const mark = this.players.size === 0 ? 'X' : 'O';
      this.players.set(actor.actorId, { actorId: actor.actorId, mark, actor });
    }
    if (this.players.size === 2 && this.status === 'WaitingForPlayers') {
      this.status = 'InProgress';
    }
    return this.players.get(actor.actorId);
  }

  onCreate(_request) {
    this.created = true;
    return { accepted: true };
  }

  onActorJoin(actor, request) {
    const admission = readJsonMessage(request);
    if (admission.gameId !== this.gameId) {
      return {
        accepted: false,
        reply: createJsonMessage({ error: `Actor '${actor.actorId}' cannot join game '${admission.gameId}'.` })
      };
    }
    const joined = this.addPlayer(actor);
    return { accepted: true, reply: createJsonMessage({ actorId: joined.actorId, mark: joined.mark }) };
  }

  place(actorId, cell) {
    const player = this.players.get(actorId);
    if (player === undefined) {
      throw new Error(`Actor '${actorId}' has not joined game '${this.gameId}'.`);
    }
    if (this.status === 'Won') {
      throw new Error(`Game '${this.gameId}' already finished.`);
    }
    if (!Number.isInteger(cell) || cell < 0 || cell > 8) {
      throw new Error(`Cell '${cell}' is outside the board.`);
    }
    if (this.cells[cell] !== '.') {
      throw new Error(`Cell '${cell}' is already occupied.`);
    }
    this.cells[cell] = player.mark;
    const winnerMark = this.winnerMark();
    if (winnerMark !== undefined) {
      this.status = 'Won';
      this.winner = [...this.players.values()].find((joined) => joined.mark === winnerMark).actorId;
    }
    return this.state();
  }

  state() {
    return {
      gameId: this.gameId,
      board: this.cells.join(''),
      status: this.status,
      winner: this.winner
    };
  }

  winnerMark() {
    for (const [a, b, c] of winningLines()) {
      if (this.cells[a] !== '.' && this.cells[a] === this.cells[b] && this.cells[a] === this.cells[c]) {
        return this.cells[a];
      }
    }
    return undefined;
  }
}

function winningLines() {
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

export { TicTacToeMatchRoom };
