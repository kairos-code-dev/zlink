const { TicTacToeGame } = require('../GameSpots/tictactoe-game');
const { PlayActorJoinGameHandler } = require('../EntrySpot/Handlers/play-actor-join-game-handler');
const { PlayActorPlaceMarkHandler } = require('../GameSpots/Handlers/play-actor-place-mark-handler');
const { TicTacToeGameJoinHandler } = require('../GameSpots/Handlers/tictactoe-game-join-handler');
const { TicTacToeGameTimerHandler } = require('../GameSpots/Handlers/tictactoe-game-timer-handler');

class CreateGameHandler {
  constructor() {
    this.joinEntrySpot = new PlayActorJoinGameHandler();
    this.joinGame = new TicTacToeGameJoinHandler();
    this.placeMark = new PlayActorPlaceMarkHandler();
    this.timer = new TicTacToeGameTimerHandler();
  }

  handle(request) {
    const notifications = [];
    const game = new TicTacToeGame(request.matchId);
    const context = { notifications };
    this.joinEntrySpot.handle(context, { matchId: request.matchId, actorId: 'p1', opponentActorId: 'p2' });
    this.joinEntrySpot.handle(context, { matchId: request.matchId, actorId: 'p2', opponentActorId: 'p1' });
    this.joinGame.handle(game, { actorId: 'p1', mark: 'X' });
    this.joinGame.handle(game, { actorId: 'p2', mark: 'O' });

    const moves = [
      ['p1', 0],
      ['p2', 3],
      ['p1', 1],
      ['p2', 4],
      ['p1', 2]
    ];
    let state;
    for (const [actorId, cell] of moves) {
      state = this.placeMark.handle(game, { actorId, cell });
      notifications.push({
        packetName: 'GameStateNotify',
        target: actorId === 'p1' ? 'p2' : 'p1',
        winner: state.winner
      });
    }

    return {
      match: request.matchId,
      firstPacket: 'CreateMatch',
      board: state.board,
      winner: state.winner,
      timerRegistered: this.timer.register().timerRegistered,
      notifications
    };
  }
}

module.exports = { CreateGameHandler };
