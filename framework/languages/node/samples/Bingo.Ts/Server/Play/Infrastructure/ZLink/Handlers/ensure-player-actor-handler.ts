import { Inject } from '@nestjs/common';
import {
  ZLINK_ACTOR_MANAGER,
  ZLINK_DRAIN_CONTROL,
  zlinkEntrySpotPacketHandler
} from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import { ActorRefWire, EnsurePlayerActorRes } from '../../../../../Shared/Contracts/bingo-messages.generated';
import type { ZLinkActorManager, ZLinkDrainControl, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import { BingoEntrySpot } from '../Spots/EntrySpot/bingo-entry-spot';
import type {
  EnsurePlayerActorReq
} from '../../../../../Shared/Contracts/messages';

@zlinkEntrySpotPacketHandler({
  entrySpot: () => BingoEntrySpot,
  packetName: PacketNames.ensurePlayerActorReq
})
class EnsurePlayerActorHandler implements
  ZLinkSpotRequestHandler<BingoEntrySpot, EnsurePlayerActorReq, EnsurePlayerActorRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(ZLINK_DRAIN_CONTROL) private readonly drain: ZLinkDrainControl
  ) {}

  async handle(_spot: BingoEntrySpot, request: EnsurePlayerActorReq): Promise<EnsurePlayerActorRes> {
    if (!this.drain.isReady()) {
      throw new Error('Draining Play node does not accept new actors.');
    }
    console.log(`play-ensure-actor request actor=${request.actorId}`);
    const actorRef = await this.actorManager.getOrCreate(request.actorId, SampleNames.playerActorType, request);
    console.log(`play-ensure-actor ready actor=${actorRef.actorId} node=${String(actorRef.nodeRid)}`);
    const actor = new ActorRefWire({
      nodeRid: String(actorRef.nodeRid),
      actorId: actorRef.actorId,
      generation: actorRef.generation.toString()
    });
    return new EnsurePlayerActorRes({ actorId: actor.actorId, actorType: SampleNames.playerActorType, actor });
  }
}

export { EnsurePlayerActorHandler };
