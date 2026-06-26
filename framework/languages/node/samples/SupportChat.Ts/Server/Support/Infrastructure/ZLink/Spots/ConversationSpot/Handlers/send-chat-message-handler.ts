import { zlinkSpotActorRequestHandler } from '@zlink-systems/nestjs';
import { ConversationSpot } from '../conversation-spot';
import { SupportUserActor } from '../../../Actors/support-user-actor';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler
} from '@zlink-systems/framework';
import type { ConversationSpot as ConversationSpotType } from '../conversation-spot';
import type { SupportUserActor as SupportUserActorType } from '../../../Actors/support-user-actor';
import type {
  SendChatMessageReq,
  SendChatMessageRes
} from '../../../../../../../Shared/Contracts/messages';

@zlinkSpotActorRequestHandler({
  actor: () => SupportUserActor,
  packetName: PacketNames.sendChatMessageReq,
  spot: () => ConversationSpot
})
class SendChatMessageHandler
  implements ZLinkSpotActorRequestHandler<ConversationSpotType, SupportUserActorType, SendChatMessageReq, SendChatMessageRes> {
  async handle(
    spot: ConversationSpotType,
    actor: SupportUserActorType,
    context: ZLinkSpotActorRequestContext,
    request: SendChatMessageReq
  ): Promise<SendChatMessageRes> {
    void context;
    return await spot.sendMessage(actor, request);
  }
}

export { SendChatMessageHandler };
