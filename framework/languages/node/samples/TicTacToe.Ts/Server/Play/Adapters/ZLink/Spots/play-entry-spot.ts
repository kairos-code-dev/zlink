const { Inject } = require('@nestjs/common');
const { PlayActorJoinGameHandler } = require('./Handlers/play-actor-join-game-handler');
import type {
  JoinGameRes,
  TicTacToeActor
} from '../../../../../Shared/Contracts/messages';

class PlayEntrySpot {
  [key: string]: any;
  constructor(joinHandler: any) {
    this.joinHandler = joinHandler;
  }

  join(actor: TicTacToeActor, roomId: string): JoinGameRes {
    return this.joinHandler.handle({ actor, roomId });
  }
}

Inject(PlayActorJoinGameHandler)(PlayEntrySpot, undefined, 0);

export { PlayEntrySpot };
