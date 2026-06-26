import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import { PlayerActorFactory } from '../Actors/player-actor-factory';
import { BingoEntrySpot } from '../Spots/EntrySpot/bingo-entry-spot';
import { SampleNames } from '../../../../Configuration/sample-names';
import {
  MatchBingoReq,
  MatchBingoRes,
  PacketNames,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.matchBingoReq)
class MatchBingoChannelHandler implements ZLinkRequestHandler<MatchBingoReq & PlayerIdentity, MatchBingoRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(PlayerActorFactory) private readonly actorFactory: PlayerActorFactory,
    @Inject(BingoEntrySpot) private readonly entrySpot: BingoEntrySpot
  ) {}

  async handle(request: MatchBingoReq & PlayerIdentity): Promise<MatchBingoRes> {
    const actorRef = await this.actorManager.getOrCreate(
      request.actorId,
      SampleNames.playerActorType,
      request
    );
    const actor = this.actorFactory.get(actorRef.actorId);
    return await this.entrySpot.matchActor(actor, request);
  }
}

export { MatchBingoChannelHandler };
