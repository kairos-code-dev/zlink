import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import { BINGO_SAMPLE_CONFIG } from '../../../../Configuration/sample-config';
import { PacketNames, allocateBingoRoomRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type { BingoSampleConfig } from '../../../../Configuration/sample-config';
import type {
  AllocateBingoRoomReq,
  AllocateBingoRoomRes,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.allocateBingoRoom)
class AllocateBingoRoomHandler implements ZLinkRequestHandler<AllocateBingoRoomReq & PlayerIdentity, AllocateBingoRoomRes> {
  constructor(
    @Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocatorType,
    @Inject(BINGO_SAMPLE_CONFIG) private readonly config: BingoSampleConfig
  ) {}

  async handle(request: AllocateBingoRoomReq & PlayerIdentity): Promise<AllocateBingoRoomRes> {
    const allocated = await this.rooms.allocate(request, this.config.playSpotNodeRid, request.mode);
    return allocateBingoRoomRes(allocated.roomId, allocated.ownerPlayNodeRid);
  }
}

Inject(BINGO_SAMPLE_CONFIG)(AllocateBingoRoomHandler, undefined, 1);

export { AllocateBingoRoomHandler };
