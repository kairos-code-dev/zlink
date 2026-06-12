import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler
} from '../../../../../../../../packages/framework/dist';
import type { BingoRoomSpot } from '../bingo-room-spot';
import type { PlayerActor } from '../../Actors/player-actor';
import type {
  SubmitBingoCardReq,
  SubmitBingoCardRes
} from '../../../../../../Shared/Contracts/messages';

class SubmitBingoCardHandler
  implements ZLinkSpotActorRequestHandler<BingoRoomSpot, PlayerActor, SubmitBingoCardReq, SubmitBingoCardRes> {
  async handle(
    room: BingoRoomSpot,
    actor: PlayerActor,
    context: ZLinkSpotActorRequestContext,
    request: SubmitBingoCardReq
  ): Promise<SubmitBingoCardRes> {
    void context;
    return await room.submitCard(actor, request);
  }
}

export { SubmitBingoCardHandler };
