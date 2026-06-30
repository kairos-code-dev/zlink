import { Injectable } from '@nestjs/common';
import type {
  ZLinkActor,
  ZLinkSpotActorSendContext,
  ZLinkSpotActorSendHandler
} from '@zlink-systems/framework';
import type { TicTacToeGameSpot } from '../tictactoe-game-spot';
import type {
  LeaveGameMsg,
  TicTacToeActor
} from '../../../../../../../Shared/Contracts/messages';

type PlayLeaveActor = TicTacToeActor & ZLinkActor;

@Injectable()
class PlayActorLeaveGameHandler
  implements ZLinkSpotActorSendHandler<TicTacToeGameSpot, PlayLeaveActor, LeaveGameMsg> {
  async handle(
    spot: TicTacToeGameSpot,
    actor: PlayLeaveActor,
    context: ZLinkSpotActorSendContext,
    request: LeaveGameMsg
  ): Promise<void> {
    void context;
    await spot.leaveGame(actor, request.roomId);
    console.log(`actor: LeaveGameMsg completed. actor=${actor.actorId}`);
  }
}

export { PlayActorLeaveGameHandler };
