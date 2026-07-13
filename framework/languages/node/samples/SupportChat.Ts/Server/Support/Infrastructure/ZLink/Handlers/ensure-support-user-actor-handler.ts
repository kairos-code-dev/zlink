import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import { zlinkActorRefSnapshotFrom } from '@zlink-systems/framework';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type {
  EnsureSupportUserActorReq,
  EnsureSupportUserActorRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.ensureSupportUserActorReq)
class EnsureSupportUserActorHandler implements ZLinkRequestHandler<EnsureSupportUserActorReq, EnsureSupportUserActorRes> {
  constructor(@Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager) {}

  async handle(request: EnsureSupportUserActorReq): Promise<EnsureSupportUserActorRes> {
    const actor = await this.actors.getOrCreate(request.actorId, 'support.user', request);
    return { actor: zlinkActorRefSnapshotFrom(actor) };
  }
}

export { EnsureSupportUserActorHandler };
