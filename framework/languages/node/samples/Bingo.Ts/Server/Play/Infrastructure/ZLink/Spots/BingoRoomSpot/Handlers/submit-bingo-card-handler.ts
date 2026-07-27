import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_MANAGER,
  ZLINK_SPOT_OUTBOUND,
  zlinkSpotActorRequestHandler
} from '@zlink-systems/nestjs';
import { BingoRoomSpot } from '../bingo-room-spot';
import { PlayerActor } from '../../../Actors/player-actor';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotManager,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type { SubmitBingoCardReq, SubmitBingoCardRes } from '../../../../../../../Shared/Contracts/messages';
import { SubmitBingoCardAtSpotReq } from './bingo-room-operation-handlers';

@zlinkSpotActorRequestHandler({
  actor: () => PlayerActor,
  packetName: PacketNames.submitBingoCardReq,
  spot: () => BingoRoomSpot
})
class SubmitBingoCardHandler
  implements ZLinkSpotActorRequestHandler<PlayerActor, SubmitBingoCardReq, SubmitBingoCardRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) private readonly spotHandles: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound
  ) {}

  async handle(
    actor: PlayerActor,
    _context: ZLinkSpotActorRequestContext,
    request: SubmitBingoCardReq
  ): Promise<SubmitBingoCardRes> {
    const spotRid = actor.context.spotRid;
    if (spotRid === undefined) throw new Error(`Bingo actor '${actor.actorId}' has no joined room.`);
    const spot = await this.spotHandles.find(spotRid);
    if (spot === undefined) throw new Error(`Bingo room '${String(spotRid)}' could not be resolved.`);
    return this.spotOutbound
      .requestToSpot(spot, new SubmitBingoCardAtSpotReq(actor.actorId, request))
      .yield<SubmitBingoCardRes>();
  }
}

export { SubmitBingoCardHandler };
