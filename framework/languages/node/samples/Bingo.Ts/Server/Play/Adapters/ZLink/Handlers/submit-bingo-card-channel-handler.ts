const { Inject } = require('@nestjs/common');
const { zlinkRequestHandler } = require('../../../../../../../../packages/nestjs/dist');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { BingoRoomAllocator } = require('../../../Application/RoomAllocation/bingo-room-allocator');
const { SubmitBingoCardHandler } = require('../Spots/Handlers/submit-bingo-card-handler');
const { BingoRoomTimerHandler } = require('../Spots/Handlers/bingo-room-timer-handler');
const { PacketNames } = require('../../../../../Shared/Contracts/messages');
import type {
  ZLinkRequestHandler,
  ZLinkSpotActorRequestContext
} from '../../../../../../../packages/framework/dist';
import type { PlayerActorFactory as PlayerActorFactoryType } from '../Actors/player-actor-factory';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type { SubmitBingoCardHandler as SubmitBingoCardHandlerType } from '../Spots/Handlers/submit-bingo-card-handler';
import type { BingoRoomTimerHandler as BingoRoomTimerHandlerType } from '../Spots/Handlers/bingo-room-timer-handler';
import type {
  PlayerIdentity,
  SubmitBingoCardReq,
  SubmitBingoCardRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.submitBingoCardReq)
class SubmitBingoCardChannelHandler implements ZLinkRequestHandler<SubmitBingoCardReq & PlayerIdentity, SubmitBingoCardRes> {
  constructor(
    @Inject(PlayerActorFactory) private readonly actorFactory: PlayerActorFactoryType,
    @Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocatorType,
    @Inject(SubmitBingoCardHandler) private readonly submitCard: SubmitBingoCardHandlerType,
    @Inject(BingoRoomTimerHandler) private readonly timer: BingoRoomTimerHandlerType
  ) {}

  async handle(request: SubmitBingoCardReq & PlayerIdentity): Promise<SubmitBingoCardRes> {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    const room = this.rooms.require(request.roomId);
    const response = await this.submitCard.handle(room, actor, createActorRequestContext(PacketNames.submitBingoCardReq), request);
    await this.timer.handle(room);
    return response;
  }
}

function createActorRequestContext(packetName: string): ZLinkSpotActorRequestContext {
  return {
    packetName,
    metadata: {},
    reply: {
      metadata() {
        return this;
      },
      compress() {
        return this;
      }
    }
  };
}

export { SubmitBingoCardChannelHandler };
