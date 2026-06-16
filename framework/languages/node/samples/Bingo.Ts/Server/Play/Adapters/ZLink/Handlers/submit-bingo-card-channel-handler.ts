import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import { BingoRoomAllocator } from '../../../Application/RoomAllocation/bingo-room-allocator';
import { SampleNames } from '../../../../Configuration/sample-names';
import type { PlayerActor } from '../Actors/player-actor';
import {
  PacketNames,
  PlayerIdentity,
  SubmitBingoCardReq,
  SubmitBingoCardRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.submitBingoCardReq)
class SubmitBingoCardChannelHandler implements ZLinkRequestHandler<SubmitBingoCardReq & PlayerIdentity, SubmitBingoCardRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(BingoRoomAllocator) private readonly rooms: BingoRoomAllocator
  ) {}

  async handle(request: SubmitBingoCardReq & PlayerIdentity): Promise<SubmitBingoCardRes> {
    const actor = await this.actorManager.getOrCreate(
      request.actorId,
      SampleNames.playerActorType
    ) as PlayerActor;
    actor.displayName = request.displayName;
    return await this.rooms.executeInRoom(request.roomId, async (room) => {
      return await room.submitCard(actor, request);
    });
  }
}

export { SubmitBingoCardChannelHandler };
