const { Inject } = require('@nestjs/common');
const { zlinkRequestHandler } = require('../../../../../../../../packages/nestjs/dist');
const { BingoRoomAllocator } = require('../../../Application/RoomAllocation/bingo-room-allocator');
const { PacketNames, allocateBingoRoomRes, BingoModes } = require('../../../../../Shared/Contracts/messages');
import type { ZLinkRequestHandler } from '../../../../../../../packages/framework/dist';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  AllocateBingoRoomReq,
  AllocateBingoRoomRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.allocateBingoRoom)
class AllocateBingoRoomHandler implements ZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
  constructor(@Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocatorType) {}

  async handle(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    const allocated = await this.rooms.allocate(request.mode ?? BingoModes.twoPlayer);
    return allocateBingoRoomRes(allocated.roomId);
  }
}

export { AllocateBingoRoomHandler };
