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
    return await room.submitCard(actor, request);
  }
}

export { SubmitBingoCardHandler };
