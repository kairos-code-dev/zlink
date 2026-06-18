import { zlinkSpotActorRequestHandler } from '@zlink-systems/nestjs';
import { ConversationSpot } from '../conversation-spot';
import { SupportUserActor } from '../../Actors/support-user-actor';
import { PacketNames } from '../../../../../../Shared/Contracts/messages';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler
} from '@zlink-systems/framework';
import type { ConversationSpot as ConversationSpotType } from '../conversation-spot';
import type { SupportUserActor as SupportUserActorType } from '../../Actors/support-user-actor';
import type {
  CloseConversationReq,
  CloseConversationRes
} from '../../../../../../Shared/Contracts/messages';

@zlinkSpotActorRequestHandler({
  actor: () => SupportUserActor,
  packetName: PacketNames.closeConversationReq,
  spot: () => ConversationSpot
})
class CloseConversationHandler
  implements ZLinkSpotActorRequestHandler<ConversationSpotType, SupportUserActorType, CloseConversationReq, CloseConversationRes> {
  async handle(
    spot: ConversationSpotType,
    actor: SupportUserActorType,
    context: ZLinkSpotActorRequestContext,
    request: CloseConversationReq
  ): Promise<CloseConversationRes> {
    void context;
    try {
      return await spot.close(actor, request);
    } catch (error) {
      return { error: error instanceof Error ? error.message : String(error) } as CloseConversationRes;
    }
  }
}

export { CloseConversationHandler };
