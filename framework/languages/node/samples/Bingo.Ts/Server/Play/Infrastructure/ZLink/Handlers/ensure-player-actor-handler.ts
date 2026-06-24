import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_GATEWAY, ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { BINGO_SAMPLE_CONFIG } from '../../../../Configuration/sample-config';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames, ensurePlayerActorRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorGateway, ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BingoSampleConfig } from '../../../../Configuration/sample-config';
import type {
  EnsurePlayerActorReq,
  EnsurePlayerActorRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.ensurePlayerActorReq)
class EnsurePlayerActorHandler implements ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(ZLINK_ACTOR_GATEWAY) private readonly actorGateway: ZLinkActorGateway,
    @Inject(BINGO_SAMPLE_CONFIG) private readonly config: BingoSampleConfig
  ) {}

  async handle(request: EnsurePlayerActorReq): Promise<EnsurePlayerActorRes> {
    console.log(`play-ensure-actor request actor=${request.actorId}`);
    const actorRef = await this.actorManager.getOrCreate(request.actorId, SampleNames.playerActorType, request);
    const joined = await this.actorGateway
      .joinEntrySpot(actorRef, this.config.playSpotNodeRid, {})
      .submit();
    console.log(`play-ensure-actor ready actor=${actorRef.actorId} node=${String(actorRef.nodeRid)}`);
    return ensurePlayerActorRes({
      nodeRid: String(actorRef.nodeRid),
      actorId: actorRef.actorId,
      generation: Number(actorRef.generation)
    });
  }
}

export { EnsurePlayerActorHandler };
