import { zlinkSpotActorRequestHandler } from '@zlink-systems/nestjs';
import { BingoRoomSpot } from '../bingo-room-spot';
import { PlayerActor } from '../../Actors/player-actor';
import { PacketNames } from '../../../../../../Shared/Contracts/messages';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler
} from '@zlink-systems/framework';
import type { BingoRoomSpot as BingoRoomSpotType } from '../bingo-room-spot';
import type { PlayerActor as PlayerActorType } from '../../Actors/player-actor';
import type {
  SubmitBingoCardReq,
  SubmitBingoCardRes
} from '../../../../../../Shared/Contracts/messages';

@zlinkSpotActorRequestHandler({
  actor: () => PlayerActor,
  packetName: PacketNames.submitBingoCardReq,
  spot: () => BingoRoomSpot
})
class SubmitBingoCardHandler
  implements ZLinkSpotActorRequestHandler<BingoRoomSpotType, PlayerActorType, SubmitBingoCardReq, SubmitBingoCardRes> {
  async handle(
    room: BingoRoomSpotType,
    actor: PlayerActorType,
    context: ZLinkSpotActorRequestContext,
    request: SubmitBingoCardReq
  ): Promise<SubmitBingoCardRes> {
    void context;
    if (process.env.BINGO_DEBUG_FLOW === '1') {
      console.log(`play-submit-card start actor=${actor.actorId}`);
    }
    const response = await room.submitCard(actor, request);
    if (process.env.BINGO_DEBUG_FLOW === '1') {
      console.log(`play-submit-card done actor=${actor.actorId}`);
    }
    return response;
  }
}

export { SubmitBingoCardHandler };
