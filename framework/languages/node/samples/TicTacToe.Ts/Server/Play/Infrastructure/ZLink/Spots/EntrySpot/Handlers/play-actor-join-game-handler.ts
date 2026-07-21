import { Injectable } from '@nestjs/common';
import type {
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import { ZLinkSpotActorRequest } from '@zlink-systems/framework';
import type { PlayActor } from '../../../Actors/play-actor';
import type {
  JoinGameReq,
  JoinGameRes,
  TicTacToeGameJoinReq,
  TicTacToeGameJoinRes
} from '../../../../../../../Shared/Contracts/messages';
import { joinGameRes } from '../../../../../../../Shared/Contracts/messages';

@Injectable()
class PlayActorJoinGameHandler {
  @ZLinkSpotActorRequest('JoinGameReq')
  async handle(
    actor: PlayActor,
    context: ZLinkSpotActorRequestContext,
    request: JoinGameReq
  ): Promise<JoinGameRes> {
    void context;
    const joinRequest: TicTacToeGameJoinReq = {
      roomId: request.roomId,
      player: {
        actorId: actor.actorId,
        displayName: actor.displayName,
        level: actor.level,
        wins: actor.wins
      }
    };
    const joined = await actor.context
      .joinSpot(request.roomId, joinRequest)
      .submit<Partial<TicTacToeGameJoinRes & { error: string }>>();
    if (joined.status === 'rejected') {
      throw new Error(
        joined.rejection.error ?? `Room '${request.roomId}' rejected actor '${actor.actorId}'.`
      );
    }
    if (joined.reply.state === undefined) {
      throw new Error(
        `Room '${request.roomId}' accepted actor '${actor.actorId}' without game state.`
      );
    }
    actor.roomId = request.roomId;
    return joinGameRes(joined.reply.state);
  }
}

export { PlayActorJoinGameHandler };
