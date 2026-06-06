const { Inject } = require('@nestjs/common');
const { PlayerActorFactory } = require('../Actors/player-actor-factory');
const { BingoEntrySpot } = require('../EntrySpot/bingo-entry-spot');
const { MatchBingoActorHandler } = require('../EntrySpot/Handlers/match-bingo-actor-handler');

class MatchBingoChannelHandler {
  constructor(actorFactory, entrySpot, matchBingoActor) {
    this.actorFactory = actorFactory;
    this.entrySpot = entrySpot;
    this.matchBingoActor = matchBingoActor;
  }

  async handle(request) {
    const actor = await this.actorFactory.ensure(request.actorId, request.displayName);
    const result = await this.matchBingoActor.handle(this.entrySpot, actor, request);
    return result;
  }
}

Inject(PlayerActorFactory)(MatchBingoChannelHandler, undefined, 0);
Inject(BingoEntrySpot)(MatchBingoChannelHandler, undefined, 1);
Inject(MatchBingoActorHandler)(MatchBingoChannelHandler, undefined, 2);

module.exports = { MatchBingoChannelHandler };
