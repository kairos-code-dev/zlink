import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SupportUserActorFactory } from '../Actors/support-user-actor-factory';
import { SupportEntrySpot } from '../Spots/support-entry-spot';
import { SampleNames } from '../../../../Configuration/sample-names';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SupportEntrySpot as SupportEntrySpotType } from '../Spots/support-entry-spot';
import type {
  OpenConversationReq,
  OpenConversationRes,
  UserIdentity
} from '../../../../../Shared/Contracts/messages';

// Relays the customer OpenConversationReq from the Session gateway to the SupportEntrySpot.
@zlinkRequestHandler('support', PacketNames.openConversationReq)
class OpenConversationChannelHandler implements ZLinkRequestHandler<OpenConversationReq & UserIdentity, OpenConversationRes> {
  constructor(
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(SupportUserActorFactory) private readonly actorFactory: SupportUserActorFactory,
    @Inject(SupportEntrySpot) private readonly entrySpot: SupportEntrySpotType
  ) {}

  async handle(request: OpenConversationReq & UserIdentity): Promise<OpenConversationRes> {
    const actorRef = await this.actorManager.getOrCreate(request.actorId, SampleNames.supportActorType, request);
    const actor = this.actorFactory.get(actorRef.actorId);
    return await this.entrySpot.openConversation(actor, request);
  }
}

export { OpenConversationChannelHandler };
