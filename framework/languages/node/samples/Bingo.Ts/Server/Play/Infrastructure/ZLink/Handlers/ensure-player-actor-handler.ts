import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { BINGO_SAMPLE_CONFIG } from '../../../../Configuration/sample-config';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames, ensurePlayerActorRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BingoSampleConfig } from '../../../../Configuration/sample-config';
import type { PlayerActor } from '../Actors/player-actor';
import type {
  EnsurePlayerActorReq,
  EnsurePlayerActorRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.ensurePlayerActorReq)
class EnsurePlayerActorHandler implements ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(BINGO_SAMPLE_CONFIG) private readonly config: BingoSampleConfig
  ) {}

  async handle(request: EnsurePlayerActorReq): Promise<EnsurePlayerActorRes> {
    console.log(`play-ensure-actor request actor=${request.actorId}`);
    const actor = await this.actorManager.getOrCreate(request.actorId, SampleNames.playerActorType) as PlayerActor;
    actor.displayName = request.displayName;
    const joined = await actor.context
      .joinEntrySpot(this.config.playSpotNodeRid, {})
      .submit();
    const actorRef = joined.actor;
    console.log(`play-ensure-actor ready actor=${actor.actorId} node=${String(actorRef.nodeRid)}`);
    return ensurePlayerActorRes({
      nodeRid: String(actorRef.nodeRid),
      actorId: actorRef.actorId,
      generation: Number(actorRef.generation)
    });
  }
}

export { EnsurePlayerActorHandler };
