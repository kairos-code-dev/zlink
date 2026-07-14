import { zlinkSpotActorRequestHandler, zlinkSpotActorSendHandler } from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import { AgentAvailabilityDirectory } from '../../../../../Application/ConversationAssignment/agent-availability-directory';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import { SupportUserActor } from '../../../Actors/support-user-actor';
import { ConversationSpot } from '../conversation-spot';
import type {
  CloseConversationReq,
  CloseConversationRes,
  JoinConversationReq,
  JoinConversationRes,
  SendChatMessageReq,
  SendChatMessageRes,
  SetTypingReq
} from '../../../../../../../Shared/Contracts/messages';
import type {
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorSendContext,
  ZLinkSpotActorSendHandler
} from '@zlink-systems/framework';

function hasConversation(context: { metadata: { find(key: string): string | undefined } }, spot: ConversationSpot): boolean {
  return context.metadata.find(SampleNames.conversationIdMetadataKey) === spot.snapshot().conversationId;
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.joinConversationReq })
class JoinConversationHandler implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, JoinConversationReq, JoinConversationRes> {
  async handle(spot: ConversationSpot, actor: SupportUserActor, context: ZLinkSpotActorRequestContext): Promise<JoinConversationRes> {
    if (!hasConversation(context, spot)) throw new Error('Conversation metadata does not match the bound actor.');
    return { state: spot.join(actor) };
  }
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.sendChatMessageReq })
class SendChatMessageHandler implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, SendChatMessageReq, SendChatMessageRes> {
  async handle(spot: ConversationSpot, actor: SupportUserActor, context: ZLinkSpotActorRequestContext, request: SendChatMessageReq): Promise<SendChatMessageRes> {
    if (!hasConversation(context, spot)) throw new Error('Conversation metadata does not match the bound actor.');
    return spot.sendChat(actor, request.text);
  }
}

@zlinkSpotActorSendHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.setTypingReq })
class SetTypingHandler implements ZLinkSpotActorSendHandler<ConversationSpot, SupportUserActor, SetTypingReq> {
  async handle(spot: ConversationSpot, actor: SupportUserActor, context: ZLinkSpotActorSendContext, request: SetTypingReq): Promise<void> {
    if (hasConversation(context, spot)) spot.setTyping(actor, request.isTyping);
  }
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.closeConversationReq })
class CloseConversationHandler implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, CloseConversationReq, CloseConversationRes> {
  constructor(private readonly availability: AgentAvailabilityDirectory) {}

  async handle(spot: ConversationSpot, actor: SupportUserActor, context: ZLinkSpotActorRequestContext): Promise<CloseConversationRes> {
    if (!hasConversation(context, spot)) throw new Error('Conversation metadata does not match the bound actor.');
    const state = await spot.close(actor);
    if (state.agentActorId !== undefined) this.availability.released(state.agentActorId);
    return { state };
  }
}

export {
  JoinConversationHandler,
  SendChatMessageHandler,
  SetTypingHandler,
  CloseConversationHandler
};
