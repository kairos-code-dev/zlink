const { Inject } = require('@nestjs/common');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { BingoRoomDirectory } = require('../../../Application/RoomAllocation/bingo-room-directory');
const { SubmitBingoCardHandler } = require('../Spots/Handlers/submit-bingo-card-handler');
const { BingoRoomTimerHandler } = require('../Spots/Handlers/bingo-room-timer-handler');
import type {
  PlayerIdentity,
  SubmitBingoCardReq,
  SubmitBingoCardRes
} from '../../../../../Shared/Contracts/messages';

class SubmitBingoCardChannelHandler {
  [key: string]: any;
  constructor(actorFactory: any, rooms: any, submitCard: any, timer: any) {
    this.actorFactory = actorFactory;
    this.rooms = rooms;
    this.submitCard = submitCard;
    this.timer = timer;
  }

  async handle(request: SubmitBingoCardReq & PlayerIdentity): Promise<SubmitBingoCardRes> {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    const room = this.rooms.require(request.roomId);
    const response = await this.submitCard.handle(room, actor, request);
    await this.timer.handle(room);
    return response;
  }
}

Inject(PlayerActorFactory)(SubmitBingoCardChannelHandler, undefined, 0);
Inject(BingoRoomDirectory)(SubmitBingoCardChannelHandler, undefined, 1);
Inject(SubmitBingoCardHandler)(SubmitBingoCardChannelHandler, undefined, 2);
Inject(BingoRoomTimerHandler)(SubmitBingoCardChannelHandler, undefined, 3);

export { SubmitBingoCardChannelHandler };
