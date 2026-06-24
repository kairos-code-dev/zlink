import { Injectable } from '@nestjs/common';
import type {
  ZLinkActor,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type { PlayEntrySpot } from '../play-entry-spot';
import type {
  JoinGameReq,
  JoinGameRes,
  TicTacToeActor
} from '../../../../../../Shared/Contracts/messages';

type PlayJoinActor = TicTacToeActor & ZLinkActor;

@Injectable()
class PlayActorJoinGameHandler
  implements ZLinkEntrySpotActorRequestHandler<PlayEntrySpot, PlayJoinActor, JoinGameReq, JoinGameRes> {
  async handle(
    entrySpot: PlayEntrySpot,
    actor: PlayJoinActor,
    context: ZLinkSpotActorRequestContext,
    request: JoinGameReq
  ): Promise<JoinGameRes> {
    void context;
    const actorRef = actor.context.actorRef;
    if (actorRef === undefined) {
      throw new Error(`Actor '${actor.actorId}' has no ActorRef.`);
    }
    return await entrySpot.join(actorRef, actor, request.roomId);
  }
}

export { PlayActorJoinGameHandler };
