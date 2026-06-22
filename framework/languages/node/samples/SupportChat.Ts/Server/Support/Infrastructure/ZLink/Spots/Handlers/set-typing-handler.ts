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
  SetTypingReq,
  SetTypingRes
} from '../../../../../../Shared/Contracts/messages';

@zlinkSpotActorRequestHandler({
  actor: () => SupportUserActor,
  packetName: PacketNames.setTypingReq,
  spot: () => ConversationSpot
})
class SetTypingHandler
  implements ZLinkSpotActorRequestHandler<ConversationSpotType, SupportUserActorType, SetTypingReq, SetTypingRes> {
  async handle(
    spot: ConversationSpotType,
    actor: SupportUserActorType,
    context: ZLinkSpotActorRequestContext,
    request: SetTypingReq
  ): Promise<SetTypingRes> {
    void context;
    return await spot.setTyping(actor, request);
  }
}

export { SetTypingHandler };
