import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import { BingoEntrySpot } from '../Spots/bingo-entry-spot';
import { SampleNames } from '../../../../Configuration/sample-names';
import {
  MatchBingoReq,
  MatchBingoRes,
  PacketNames,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';
import type { PlayerActor } from '../Actors/player-actor';

@zlinkRequestHandler('play', PacketNames.matchBingoReq)
class MatchBingoChannelHandler implements ZLinkRequestHandler<MatchBingoReq & PlayerIdentity, MatchBingoRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(BingoEntrySpot) private readonly entrySpot: BingoEntrySpot
  ) {}

  async handle(request: MatchBingoReq & PlayerIdentity): Promise<MatchBingoRes> {
    const actor = await this.actorManager.getOrCreate(
      request.actorId,
      SampleNames.playerActorType
    ) as PlayerActor;
    actor.displayName = request.displayName;
    return await this.entrySpot.matchActor(actor, request);
  }
}

export { MatchBingoChannelHandler };
