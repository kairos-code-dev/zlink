import { Injectable } from '@nestjs/common';
import type {
  ZLinkActor,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import { ZLinkSpotActorRequest } from '@zlink-systems/framework';
import type { PlayEntrySpot } from '../play-entry-spot';
import type {
  JoinGameReq,
  JoinGameRes,
  TicTacToeActor
} from '../../../../../../../Shared/Contracts/messages';

type PlayJoinActor = TicTacToeActor & ZLinkActor;

@Injectable()
class PlayActorJoinGameHandler
  implements ZLinkEntrySpotActorRequestHandler<PlayEntrySpot, PlayJoinActor, JoinGameReq, JoinGameRes> {
  @ZLinkSpotActorRequest('JoinGameReq')
  async handle(
    entrySpot: PlayEntrySpot,
    actor: PlayJoinActor,
    context: ZLinkSpotActorRequestContext,
    request: JoinGameReq
  ): Promise<JoinGameRes> {
    void context;
    return await entrySpot.join(actor, actor, request.roomId);
  }
}

export { PlayActorJoinGameHandler };
