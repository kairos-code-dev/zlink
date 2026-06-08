const { Inject } = require('@nestjs/common');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { BingoEntrySpot } = require('../EntrySpot/bingo-entry-spot');
const { MatchBingoActorHandler } = require('../EntrySpot/Handlers/match-bingo-actor-handler');
import type {
  MatchBingoReq,
  MatchBingoRes,
  PlayerIdentity
} from '../../../Shared/Contracts/messages';

class MatchBingoChannelHandler {
  [key: string]: any;
  constructor(actorFactory: any, entrySpot: any, matchBingoActor: any) {
    this.actorFactory = actorFactory;
    this.entrySpot = entrySpot;
    this.matchBingoActor = matchBingoActor;
  }
  async handle(request: MatchBingoReq & PlayerIdentity): Promise<MatchBingoRes> {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    return await this.matchBingoActor.handle(this.entrySpot, actor, request);
  }
}

Inject(PlayerActorFactory)(MatchBingoChannelHandler, undefined, 0);
Inject(BingoEntrySpot)(MatchBingoChannelHandler, undefined, 1);
Inject(MatchBingoActorHandler)(MatchBingoChannelHandler, undefined, 2);

export { MatchBingoChannelHandler };
