import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import { PacketNames, allocateBingoRoomRes, BingoModes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  AllocateBingoRoomReq,
  AllocateBingoRoomRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.allocateBingoRoom)
class AllocateBingoRoomHandler implements ZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
  constructor(@Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocatorType) {}

  async handle(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    const roomId = await this.rooms.allocate(request.mode ?? BingoModes.twoPlayer);
    return allocateBingoRoomRes(roomId);
  }
}

export { AllocateBingoRoomHandler };
