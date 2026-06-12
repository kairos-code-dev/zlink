const { Inject } = require('@nestjs/common');
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

class SubmitBingoCardChannelHandler implements ZLinkRequestHandler<SubmitBingoCardReq & PlayerIdentity, SubmitBingoCardRes> {
  constructor(
    private readonly actorFactory: PlayerActorFactoryType,
    private readonly rooms: BingoRoomAllocatorType,
    private readonly submitCard: SubmitBingoCardHandlerType,
    private readonly timer: BingoRoomTimerHandlerType
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

Inject(PlayerActorFactory)(SubmitBingoCardChannelHandler, undefined, 0);
Inject(BingoRoomAllocator)(SubmitBingoCardChannelHandler, undefined, 1);
Inject(SubmitBingoCardHandler)(SubmitBingoCardChannelHandler, undefined, 2);
Inject(BingoRoomTimerHandler)(SubmitBingoCardChannelHandler, undefined, 3);

export { SubmitBingoCardChannelHandler };
