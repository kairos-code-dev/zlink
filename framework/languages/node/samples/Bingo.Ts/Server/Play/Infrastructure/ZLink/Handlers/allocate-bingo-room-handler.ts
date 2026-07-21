import { Inject } from '@nestjs/common';
import {
  ZLINK_ALLOCATED_ROUTING_ID_PROVIDER,
  ZLINK_ROUTE_CLIENT,
  ZLINK_SPOT_HANDLE_RESOLVER,
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
  ZLinkAllocatedRoutingIdProvider,
  ZLinkRequestHandler,
  ZLinkRouteClient,
  ZLinkSpotHandleResolver,
  ZLinkSpotRequestHandler,
  ZLinkSpotManager
} from '@zlink-systems/framework';
import type { BingoRoomAllocator as BingoRoomAllocatorType } from '../../../Application/RoomAllocation/bingo-room-allocator';

class BingoRoomProvisioner {
  constructor(
    @Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocatorType,
    @Inject(ZLINK_ALLOCATED_ROUTING_ID_PROVIDER)
    private readonly allocatedRoutingIds: ZLinkAllocatedRoutingIdProvider,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async localNodeRid(): Promise<string> {
    const allocation = await this.allocatedRoutingIds.waitForReadyAllocation('bingo.play');
    const localNodeRid = allocation.memberRoutingIds.get(SampleNames.roomSpotNode);
    if (localNodeRid === undefined) {
      throw new Error("Bingo allocation group 'bingo.play' did not allocate the room MeshNode.");
    }
    return localNodeRid;
  }

  async allocate(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    const localNodeRid = await this.localNodeRid();
    const allocated = await this.rooms.allocate(
      request,
      request.preferredOwnerNodeRid.length > 0 ? request.preferredOwnerNodeRid : localNodeRid,
      request.mode
    );
    if (allocated.created && allocated.ownerPlayNodeRid === localNodeRid) {
      await this.spots.getOrCreate(
        SampleNames.roomSpotNode,
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

@zlinkRequestHandler('play', PacketNames.allocateBingoRoom)
class AllocateBingoRoomHandler implements ZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes> {
  constructor(
    private readonly provisioner: BingoRoomProvisioner,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_ROUTE_CLIENT) private readonly routes: ZLinkRouteClient
  ) {}

  async handle(request: AllocateBingoRoomReq): Promise<AllocateBingoRoomRes> {
    const localNodeRid = await this.provisioner.localNodeRid();
    if (request.preferredOwnerNodeRid.length === 0 || request.preferredOwnerNodeRid === localNodeRid) {
      return await this.provisioner.allocate(request);
    }
    const preferredEntrySpot = await this.spotHandles.resolveSpotHandle(
      SampleNames.roomSpotNode,
      request.preferredOwnerNodeRid
    );
    if (preferredEntrySpot === undefined) {
      throw new Error(`Preferred Play entry spot '${request.preferredOwnerNodeRid}' was not found.`);
    }
    const forwarded = await this.routes
      .requestToSpot(preferredEntrySpot, new AllocateBingoRoomReq({
        mode: request.mode,
        actorId: request.actorId,
        preferredOwnerNodeRid: request.preferredOwnerNodeRid
      }))
      .submit<AllocateBingoRoomRes>();
    return new AllocateBingoRoomRes({
      roomId: forwarded.roomId,
      roomOwnerNodeRid: forwarded.roomOwnerNodeRid
    });
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

Inject(ZLINK_ALLOCATED_ROUTING_ID_PROVIDER)(BingoRoomProvisioner, undefined, 1);
Inject(ZLINK_SPOT_MANAGER)(BingoRoomProvisioner, undefined, 2);

export { AllocateBingoRoomHandler, AllocateBingoRoomSpotHandler, BingoRoomProvisioner };
