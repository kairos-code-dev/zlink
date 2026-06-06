const { Inject } = require('@nestjs/common');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { BingoRoomDirectory } = require('./bingo-room-directory');
const { StartBingoGameHandler } = require('../BingoRoomSpots/Handlers/start-bingo-game-handler');
const { BingoRoomTimerHandler } = require('../BingoRoomSpots/Handlers/bingo-room-timer-handler');

class StartBingoGameChannelHandler {
  [key: string]: any;
  constructor(actorFactory, rooms, startBingoGame, timer) {
    this.actorFactory = actorFactory;
    this.rooms = rooms;
    this.startBingoGame = startBingoGame;
    this.timer = timer;
  }
  async handle(request) {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    const room = this.rooms.require(request.roomId);
    try {
      const started = await this.startBingoGame.handle(room, actor, request);
      if (started.state.status === 'Running') {
        await this.timer.handle(room);
      }
      return started;
    } catch (error) {
      return {
        rejected: true,
        reason: error.message
      };
    }
  }
}

Inject(PlayerActorFactory)(StartBingoGameChannelHandler, undefined, 0);
Inject(BingoRoomDirectory)(StartBingoGameChannelHandler, undefined, 1);
Inject(StartBingoGameHandler)(StartBingoGameChannelHandler, undefined, 2);
Inject(BingoRoomTimerHandler)(StartBingoGameChannelHandler, undefined, 3);

export { StartBingoGameChannelHandler };
