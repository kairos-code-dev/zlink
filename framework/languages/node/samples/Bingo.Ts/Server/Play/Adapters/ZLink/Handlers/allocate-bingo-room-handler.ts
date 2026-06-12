const { Inject } = require('@nestjs/common');
const { BingoRoomAllocator } = require('../../../Application/RoomAllocation/bingo-room-allocator');
const { allocateBingoRoomRes, BingoModes } = require('../../../../../Shared/Contracts/messages');
import type { ZLinkRequestHandler } from '../../../../../../../packages/framework/dist';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  AllocateBingoRoomReq,
  AllocateBingoRoomRes
} from '../../../../../Shared/Contracts/messages';

class AllocateBingoRoomHandler implements ZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
  constructor(private readonly rooms: BingoRoomAllocatorType) {}
  async handle(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    const allocated = await this.rooms.allocate(request.mode ?? BingoModes.twoPlayer);
    return allocateBingoRoomRes(allocated.roomId);
  }
}

Inject(BingoRoomAllocator)(AllocateBingoRoomHandler, undefined, 0);

export { AllocateBingoRoomHandler };
