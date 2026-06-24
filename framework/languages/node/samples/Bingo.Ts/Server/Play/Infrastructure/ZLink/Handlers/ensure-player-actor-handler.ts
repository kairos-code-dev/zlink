import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames, ensurePlayerActorRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  EnsurePlayerActorReq,
  EnsurePlayerActorRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('play', PacketNames.ensurePlayerActorReq)
class EnsurePlayerActorHandler implements ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager
  ) {}

  async handle(request: EnsurePlayerActorReq): Promise<EnsurePlayerActorRes> {
    console.log(`play-ensure-actor request actor=${request.actorId}`);
    const actorRef = await this.actorManager.getOrCreate(request.actorId, SampleNames.playerActorType, request);
    console.log(`play-ensure-actor ready actor=${actorRef.actorId} node=${String(actorRef.nodeRid)}`);
    return ensurePlayerActorRes({
      nodeRid: String(actorRef.nodeRid),
      actorId: actorRef.actorId,
      generation: Number(actorRef.generation)
    });
  }
}

export { EnsurePlayerActorHandler };
