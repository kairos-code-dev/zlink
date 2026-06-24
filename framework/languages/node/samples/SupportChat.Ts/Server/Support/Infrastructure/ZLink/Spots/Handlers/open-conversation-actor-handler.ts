import { zlinkEntrySpotActorRequestHandler } from '@zlink-systems/nestjs';
import { SupportEntrySpot } from '../support-entry-spot';
import { SupportUserActor } from '../../Actors/support-user-actor';
import { PacketNames } from '../../../../../../Shared/Contracts/messages';
import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import type { SupportEntrySpot as SupportEntrySpotType } from '../support-entry-spot';
import type { SupportUserActor as SupportUserActorType } from '../../Actors/support-user-actor';
import type {
  OpenConversationReq,
  OpenConversationRes
} from '../../../../../../Shared/Contracts/messages';

@zlinkEntrySpotActorRequestHandler({
  actor: () => SupportUserActor,
  entrySpot: () => SupportEntrySpot,
  packetName: PacketNames.openConversationReq
})
class OpenConversationActorHandler
  implements ZLinkEntrySpotActorRequestHandler<SupportEntrySpotType, SupportUserActorType, OpenConversationReq, OpenConversationRes> {
  async handle(
    entrySpot: SupportEntrySpotType,
    actor: SupportUserActorType,
    context: ZLinkSpotActorRequestContext,
    request: OpenConversationReq
  ): Promise<OpenConversationRes> {
    void context;
    return await entrySpot.openConversation(actor, {
      ...request,
      actorId: actor.actorId,
      displayName: actor.displayName,
      role: actor.role
    });
  }
}

export { OpenConversationActorHandler };
