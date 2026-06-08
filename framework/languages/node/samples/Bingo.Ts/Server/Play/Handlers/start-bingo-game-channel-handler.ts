const { Inject } = require('@nestjs/common');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { BingoRoomDirectory } = require('./bingo-room-directory');
const { StartBingoGameHandler } = require('../BingoRoomSpots/Handlers/start-bingo-game-handler');
const { BingoRoomTimerHandler } = require('../BingoRoomSpots/Handlers/bingo-room-timer-handler');
const { rejectedCommandRes } = require('../../../Shared/Contracts/messages');
import type {
  PlayerIdentity,
  RejectedCommandRes,
  StartBingoGameReq,
  StateEnvelope
} from '../../../Shared/Contracts/messages';

class StartBingoGameChannelHandler {
  [key: string]: any;
  constructor(actorFactory: any, rooms: any, startBingoGame: any, timer: any) {
    this.actorFactory = actorFactory;
    this.rooms = rooms;
    this.startBingoGame = startBingoGame;
    this.timer = timer;
  }
  async handle(request: StartBingoGameReq & PlayerIdentity): Promise<StateEnvelope | RejectedCommandRes> {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    const room = this.rooms.require(request.roomId);
    try {
      const started = await this.startBingoGame.handle(room, actor, request);
      if (started.state.status === 'Running') {
        await this.timer.handle(room);
      }
      return started;
    } catch (error) {
      const reason = error instanceof Error ? error.message : String(error);
      return rejectedCommandRes(reason);
    }
  }
}

Inject(PlayerActorFactory)(StartBingoGameChannelHandler, undefined, 0);
Inject(BingoRoomDirectory)(StartBingoGameChannelHandler, undefined, 1);
Inject(StartBingoGameHandler)(StartBingoGameChannelHandler, undefined, 2);
Inject(BingoRoomTimerHandler)(StartBingoGameChannelHandler, undefined, 3);

export { StartBingoGameChannelHandler };
