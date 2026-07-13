import { Inject } from '@nestjs/common';
import { ZLINK_SPOT_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import { BINGO_SAMPLE_CONFIG } from '../../../../Configuration/sample-config';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import { AllocateBingoRoomRes, BingoRoomSettingsPayload } from '../../../../../Shared/Contracts/bingo-messages.generated';
import { BingoRoomSpot } from '../Spots/BingoRoomSpot/bingo-room-spot';
import type { ZLinkRequestHandler, ZLinkSpotManager } from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';
import type { BingoSampleConfig } from '../../../../Configuration/sample-config';
import type {
  AllocateBingoRoomReq
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.allocateBingoRoom)
class AllocateBingoRoomHandler implements ZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
  constructor(
    @Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocatorType,
    @Inject(BINGO_SAMPLE_CONFIG) private readonly config: BingoSampleConfig,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async handle(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    const allocated = await this.rooms.allocate(
      request,
      request.preferredOwnerNodeRid.length > 0 ? request.preferredOwnerNodeRid : this.config.playSpotNodeRid,
      request.mode
    );
    if (allocated.created && allocated.ownerPlayNodeRid === this.config.playSpotNodeRid) {
      await this.spots.getOrCreate(
        BingoRoomSpot,
        allocated.roomId,
        new BingoRoomSettingsPayload({
          ...allocated.settings,
          purpose: allocated.settings.purpose,
          observedRoomId: allocated.settings.observedRoomId
        })
      );
    }
    return new AllocateBingoRoomRes({ roomId: allocated.roomId, roomOwnerNodeRid: allocated.ownerPlayNodeRid });
  }
}

Inject(BINGO_SAMPLE_CONFIG)(AllocateBingoRoomHandler, undefined, 1);
Inject(ZLINK_SPOT_MANAGER)(AllocateBingoRoomHandler, undefined, 2);

export { AllocateBingoRoomHandler };
