const { Inject } = require('@nestjs/common');
const { TicTacToeMatchDirectory } = require('../../GameSpots/tictactoe-match-directory');
const { SessionGatewayActorManager } = require('../../session-gateway-actor-manager');

class JoinMatchHandler {
  constructor(actorManager, matches) {
    this.actorManager = actorManager;
    this.matches = matches;
  }

  async handle(request) {
    const actor = await this.actorManager.getOrCreate(request.actorId, 'player');
    const room = this.matches.require(request.matchId);
    const state = await room.join(actor);
    return {
      matchId: room.matchId,
      actorId: actor.actorId,
      state
    };
  }
}

Inject(SessionGatewayActorManager)(JoinMatchHandler, undefined, 0);
Inject(TicTacToeMatchDirectory)(JoinMatchHandler, undefined, 1);

module.exports = { JoinMatchHandler };
