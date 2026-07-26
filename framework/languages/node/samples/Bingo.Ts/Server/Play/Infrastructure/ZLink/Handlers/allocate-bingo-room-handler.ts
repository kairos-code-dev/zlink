import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_MANAGER,
  zlinkEntrySpotPacketHandler,
  zlinkRequestHandler
} from '@zlink-systems/nestjs';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import {
  AllocateBingoRoomReq,
  AllocateBingoRoomRes,
  BingoRoomSettingsPayload
} from '../../../../../Shared/Contracts/bingo-messages.generated';
import { BingoRoomSpot } from '../Spots/BingoRoomSpot/bingo-room-spot';
import { BingoEntrySpot } from '../Spots/EntrySpot/bingo-entry-spot';
import type {
  ZLinkRequestHandler,
  ZLinkSpotRequestHandler,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';

class BingoRoomProvisioner {
  constructor(
    @Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocatorType,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async allocate(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    const allocated = await this.rooms.allocate(
      request,
      request.mode
    );
    if (allocated.created) {
      await this.spots
        .getOrCreate(allocated.roomId, SampleNames.roomSpotType)
        .inMesh(SampleNames.roomSpotNode)
        .request(new BingoRoomSettingsPayload({
          ...allocated.settings,
          purpose: allocated.settings.purpose,
          observedRoomId: allocated.settings.observedRoomId
        }))
        .submit();
    }
    return new AllocateBingoRoomRes({ roomId: allocated.roomId });
  }
}

@zlinkRequestHandler('play', PacketNames.allocateBingoRoom)
class AllocateBingoRoomHandler implements ZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
  constructor(
    private readonly provisioner: BingoRoomProvisioner
  ) {}

  async handle(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    return await this.provisioner.allocate(request);
  }
}

@zlinkEntrySpotPacketHandler({
  entrySpot: () => BingoEntrySpot,
  packetName: PacketNames.allocateBingoRoom
})
class AllocateBingoRoomSpotHandler implements
  ZLinkSpotRequestHandler<BingoEntrySpot, AllocateBingoRoomReq, AllocateBingoRoomRes> {
  constructor(private readonly provisioner: BingoRoomProvisioner) {}

  async handle(_spot: BingoEntrySpot, request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    return await this.provisioner.allocate(request);
  }
}

Inject(ZLINK_SPOT_MANAGER)(BingoRoomProvisioner, undefined, 1);

export { AllocateBingoRoomHandler, AllocateBingoRoomSpotHandler, BingoRoomProvisioner };
