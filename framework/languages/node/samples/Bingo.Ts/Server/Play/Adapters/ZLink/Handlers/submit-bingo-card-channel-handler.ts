import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, ZLINK_SPOT_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import type { ZLinkActorManager, ZLinkRequestHandler, ZLinkSpotManager } from '@zlink-systems/framework';
import { SampleNames } from '../../../../Configuration/sample-names';
import { BingoRoomSpot } from '../Spots/bingo-room-spot';
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
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async handle(request: SubmitBingoCardReq & PlayerIdentity): Promise<SubmitBingoCardRes> {
    const actor = await this.actorManager.getOrCreate(
      request.actorId,
      SampleNames.playerActorType
    ) as PlayerActor;
    actor.displayName = request.displayName;
    return await this.spots.executeOnSpot<BingoRoomSpot, SubmitBingoCardRes>(BingoRoomSpot, request.roomId, async (room) => {
      return await room.submitCard(actor, request);
    });
  }
}

export { SubmitBingoCardChannelHandler };
