import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_OUTBOUND,
  zlinkSpotActorRequestHandler,
  zlinkSpotActorSendHandler
} from '@zlink-systems/nestjs';
import { SampleNames } from '../../../../../../Configuration/sample-names';
import { AgentAvailabilityDirectory } from '../../../../../Application/ConversationAssignment/agent-availability-directory';
import { PacketNames } from '../../../../../../../Shared/Contracts/messages';
import { SupportUserActor } from '../../../Actors/support-user-actor';
import { ConversationSpot } from '../conversation-spot';
import {
  CloseConversationAtSpotReq,
  JoinConversationAtSpotReq,
  SendChatMessageAtSpotReq,
  SetTypingAtSpotMsg
} from './conversation-operation-handlers';
import type {
  CloseConversationReq,
  CloseConversationRes,
  JoinConversationReq,
  JoinConversationRes,
  SendChatMessageReq,
  SendChatMessageRes,
  SetTypingMsg
} from '../../../../../../../Shared/Contracts/messages';
import type {
  ZLinkMessageContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorSendHandler,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';

abstract class ConversationActorRoute {
  constructor(protected readonly spotOutbound: ZLinkSpotOutbound) {}

  protected resolve(actor: SupportUserActor, context: ZLinkMessageContext): string {
    const conversationId = context.metadata.find(SampleNames.conversationIdMetadataKey);
    if (conversationId === undefined || String(actor.context.spotId) !== conversationId) {
      throw new Error('Conversation metadata does not match the actor membership.');
    }
    return conversationId;
  }
}

@zlinkSpotActorRequestHandler({
  actor: () => SupportUserActor,
  spot: () => ConversationSpot,
  packetName: PacketNames.joinConversationReq
})
class JoinConversationHandler extends ConversationActorRoute
  implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, JoinConversationReq, JoinConversationRes> {
  constructor(@Inject(ZLINK_SPOT_OUTBOUND) outbound: ZLinkSpotOutbound) {
    super(outbound);
  }

  async handle(
    _spot: ConversationSpot,
    actor: SupportUserActor,
    context: ZLinkMessageContext
  ): Promise<JoinConversationRes> {
    return this.spotOutbound
      .requestToSpot(this.resolve(actor, context), new JoinConversationAtSpotReq(actor.actorId))
      .submit<JoinConversationRes>();
  }
}

@zlinkSpotActorRequestHandler({
  actor: () => SupportUserActor,
  spot: () => ConversationSpot,
  packetName: PacketNames.sendChatMessageReq
})
class SendChatMessageHandler extends ConversationActorRoute
  implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, SendChatMessageReq, SendChatMessageRes> {
  constructor(@Inject(ZLINK_SPOT_OUTBOUND) outbound: ZLinkSpotOutbound) {
    super(outbound);
  }

  async handle(
    _spot: ConversationSpot,
    actor: SupportUserActor,
    context: ZLinkMessageContext,
    request: SendChatMessageReq
  ): Promise<SendChatMessageRes> {
    return this.spotOutbound
      .requestToSpot(
        this.resolve(actor, context),
        new SendChatMessageAtSpotReq(actor.actorId, request.text)
      )
      .submit<SendChatMessageRes>();
  }
}

@zlinkSpotActorSendHandler({
  actor: () => SupportUserActor,
  spot: () => ConversationSpot,
  packetName: PacketNames.setTypingMsg
})
class SetTypingHandler extends ConversationActorRoute
  implements ZLinkSpotActorSendHandler<ConversationSpot, SupportUserActor, SetTypingMsg> {
  constructor(@Inject(ZLINK_SPOT_OUTBOUND) outbound: ZLinkSpotOutbound) {
    super(outbound);
  }

  async handle(
    _spot: ConversationSpot,
    actor: SupportUserActor,
    context: ZLinkMessageContext,
    request: SetTypingMsg
  ): Promise<void> {
    await this.spotOutbound
      .sendToSpot(
        this.resolve(actor, context),
        new SetTypingAtSpotMsg(actor.actorId, request.isTyping)
      )
      .submit();
  }
}

@zlinkSpotActorRequestHandler({
  actor: () => SupportUserActor,
  spot: () => ConversationSpot,
  packetName: PacketNames.closeConversationReq
})
class CloseConversationHandler extends ConversationActorRoute
  implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, CloseConversationReq, CloseConversationRes> {
  constructor(
    @Inject(ZLINK_SPOT_OUTBOUND) outbound: ZLinkSpotOutbound,
    private readonly availability: AgentAvailabilityDirectory
  ) {
    super(outbound);
  }

  async handle(
    _spot: ConversationSpot,
    actor: SupportUserActor,
    context: ZLinkMessageContext
  ): Promise<CloseConversationRes> {
    const response = await this.spotOutbound
      .requestToSpot(
        this.resolve(actor, context),
        new CloseConversationAtSpotReq(actor.actorId)
      )
      .submit<CloseConversationRes>();
    if (response.state.agentActorId !== undefined) {
      this.availability.released(response.state.agentActorId);
    }
    return response;
  }
}

export {
  JoinConversationHandler,
  SendChatMessageHandler,
  SetTypingHandler,
  CloseConversationHandler
};
