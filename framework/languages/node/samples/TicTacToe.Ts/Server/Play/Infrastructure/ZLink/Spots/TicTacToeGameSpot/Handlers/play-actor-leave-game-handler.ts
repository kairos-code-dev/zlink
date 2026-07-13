import { Injectable } from '@nestjs/common';
import type {
  ZLinkActor,
  ZLinkSpotActorSendContext,
  ZLinkSpotActorSendHandler
} from '@zlink-systems/framework';
import { ZLinkSpotActorSend } from '@zlink-systems/framework';
import type { TicTacToeGameSpot } from '../tictactoe-game-spot';
import type {
  LeaveGameReq,
  TicTacToeActor
} from '../../../../../../../Shared/Contracts/messages';

type PlayLeaveActor = TicTacToeActor & ZLinkActor;

@Injectable()
class PlayActorLeaveGameHandler
  implements ZLinkSpotActorSendHandler<TicTacToeGameSpot, PlayLeaveActor, LeaveGameReq> {
  @ZLinkSpotActorSend('LeaveGameReq')
  async handle(
    spot: TicTacToeGameSpot,
    actor: PlayLeaveActor,
    context: ZLinkSpotActorSendContext,
    request: LeaveGameReq
  ): Promise<void> {
    void context;
    await spot.leaveGame(actor, request.roomId);
    console.log(`actor: LeaveGameReq completed. actor=${actor.actorId}`);
  }
}

export { PlayActorLeaveGameHandler };
