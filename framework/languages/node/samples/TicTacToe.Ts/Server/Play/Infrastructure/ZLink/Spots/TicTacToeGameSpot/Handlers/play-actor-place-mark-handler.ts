import { Injectable } from '@nestjs/common';
import type {
  ZLinkActor,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler
} from '@zlink-systems/framework';
import type { TicTacToeGameSpot } from '../tictactoe-game-spot';
import type {
  PlaceMarkReq,
  PlaceMarkRes,
  TicTacToeActor
} from '../../../../../../../Shared/Contracts/messages';

type PlayPlaceMarkActor = TicTacToeActor & ZLinkActor;

@Injectable()
class PlayActorPlaceMarkHandler
  implements ZLinkSpotActorRequestHandler<TicTacToeGameSpot, PlayPlaceMarkActor, PlaceMarkReq, PlaceMarkRes> {
  async handle(
    spot: TicTacToeGameSpot,
    actor: PlayPlaceMarkActor,
    context: ZLinkSpotActorRequestContext,
    request: PlaceMarkReq
  ): Promise<PlaceMarkRes> {
    void context;
    return await spot.placeMark(actor, request.cell);
  }
}

export { PlayActorPlaceMarkHandler };
