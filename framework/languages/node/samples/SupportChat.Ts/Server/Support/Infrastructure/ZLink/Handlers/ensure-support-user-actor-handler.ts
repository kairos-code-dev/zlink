import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames, ensureSupportUserActorRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SupportUserActor } from '../Actors/support-user-actor';
import type {
  EnsureSupportUserActorReq,
  EnsureSupportUserActorRes
} from '../../../../../Shared/Contracts/messages';

// EnsureSupportUserActorHandler creates the customer/agent actor on first authentication or
// returns the existing actor on reconnect, keeping the actor's identity (display name, role)
// and any current conversation membership. Reconnect never makes a new actor.
@zlinkRequestHandler('support', PacketNames.ensureSupportUserActorReq)
class EnsureSupportUserActorHandler implements ZLinkRequestHandler<EnsureSupportUserActorReq, EnsureSupportUserActorRes> {
  constructor(@Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager) {}

  async handle(request: EnsureSupportUserActorReq): Promise<EnsureSupportUserActorRes> {
    const actor = await this.actorManager.getOrCreate(request.actorId, SampleNames.supportActorType) as SupportUserActor;
    actor.setIdentity(request.displayName, request.role);
    return ensureSupportUserActorRes(actor.actorRef);
  }
}

export { EnsureSupportUserActorHandler };
