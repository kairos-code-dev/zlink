const { Injectable } = require('@nestjs/common');
const { TicTacToeGameSpot } = require('../tictactoe-game-spot');
import type { ZLinkActor } from '../../../../../../../../packages/framework/dist';
import type {
  PlaceMarkInternalReq,
  PlaceMarkRes
} from '../../../../../../Shared/Contracts/messages';

@Injectable()
class PlayActorPlaceMarkHandler {
  async handle(request: PlaceMarkInternalReq): Promise<PlaceMarkRes> {
    if (request.actor.roomId === undefined) {
      throw new Error(`Actor '${request.actor.actorId}' has not joined a room.`);
    }
    const actor = request.actor as unknown as ZLinkActor & typeof request.actor;
    const spot = actor.context.getSpot(TicTacToeGameSpot) as InstanceType<typeof TicTacToeGameSpot>;
    return await spot.placeMark(actor, request.cell);
  }
}

export { PlayActorPlaceMarkHandler };
