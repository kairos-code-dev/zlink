import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import { PacketNames, allocateBingoRoomRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type {
  AllocateBingoRoomReq,
  AllocateBingoRoomRes,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.allocateBingoRoom)
class AllocateBingoRoomHandler implements ZLinkRequestHandler<AllocateBingoRoomReq & PlayerIdentity, AllocateBingoRoomRes> {
  constructor(@Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocatorType) {}

  async handle(request: AllocateBingoRoomReq & PlayerIdentity): Promise<AllocateBingoRoomRes> {
    const allocated = await this.rooms.allocate(request, request.mode);
    return allocateBingoRoomRes(allocated.roomId, allocated.ownerPlayNodeRid);
  }
}

export { AllocateBingoRoomHandler };
