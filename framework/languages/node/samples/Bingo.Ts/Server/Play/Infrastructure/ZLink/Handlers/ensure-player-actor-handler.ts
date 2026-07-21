import { Inject } from '@nestjs/common';
import {
  ZLINK_ACTOR_MANAGER,
  ZLINK_ROUTE_MESH_RUNTIME,
  zlinkEntrySpotPacketHandler
} from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import {
  ActorRefWire,
  EnsurePlayerActorReq as GeneratedEnsurePlayerActorReq,
  EnsurePlayerActorRes
} from '../../../../../Shared/Contracts/bingo-messages.generated';
import type { ZLinkActorManager, ZLinkRouteMeshRuntime, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
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
    @Inject(ZLINK_ROUTE_MESH_RUNTIME) private readonly routeMeshRuntime: ZLinkRouteMeshRuntime
  ) {}

  async handle(_spot: BingoEntrySpot, request: EnsurePlayerActorReq): Promise<EnsurePlayerActorRes> {
    if (!this.routeMeshRuntime.isReady(SampleNames.roomSpotNode)) {
      throw new Error('Draining Play node does not accept new actors.');
    }
    console.log(`play-ensure-actor request actor=${request.actorId}`);
    const actorRef = await this.actorManager.getOrCreate(
      SampleNames.roomSpotNode,
      request.actorId,
      SampleNames.playerActorType,
      new GeneratedEnsurePlayerActorReq({
        actorId: request.actorId,
        displayName: request.displayName,
        preferredActorNodeRid: request.preferredActorNodeRid
      })
    );
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
