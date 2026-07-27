import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_MANAGER,
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
  SetTypingReq
} from '../../../../../../../Shared/Contracts/messages';
import type {
  SpotRef,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorSendContext,
  ZLinkSpotActorSendHandler,
  ZLinkSpotManager,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';

abstract class ConversationActorRoute {
  constructor(
    protected readonly spotHandles: ZLinkSpotManager,
    protected readonly spotOutbound: ZLinkSpotOutbound
  ) {}

  protected async resolve(actor: SupportUserActor, context: ZLinkSpotActorSendContext): Promise<SpotRef> {
    const conversationId = context.metadata.find(SampleNames.conversationIdMetadataKey);
    if (conversationId === undefined || String(actor.context.spotRid) !== conversationId) {
      throw new Error('Conversation metadata does not match the actor membership.');
    }
    const spot = await this.spotHandles.find(conversationId);
    if (spot === undefined) throw new Error(`Conversation '${conversationId}' could not be resolved.`);
    return spot;
  }
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.joinConversationReq })
class JoinConversationHandler extends ConversationActorRoute
  implements ZLinkSpotActorRequestHandler<SupportUserActor, JoinConversationReq, JoinConversationRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) handles: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) outbound: ZLinkSpotOutbound
  ) { super(handles, outbound); }

  async handle(actor: SupportUserActor, context: ZLinkSpotActorRequestContext): Promise<JoinConversationRes> {
    const spot = await this.resolve(actor, context);
    return this.spotOutbound.requestToSpot(spot, new JoinConversationAtSpotReq(actor.actorId)).yield<JoinConversationRes>();
  }
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.sendChatMessageReq })
class SendChatMessageHandler extends ConversationActorRoute
  implements ZLinkSpotActorRequestHandler<SupportUserActor, SendChatMessageReq, SendChatMessageRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) handles: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) outbound: ZLinkSpotOutbound
  ) { super(handles, outbound); }

  async handle(actor: SupportUserActor, context: ZLinkSpotActorRequestContext, request: SendChatMessageReq): Promise<SendChatMessageRes> {
    const spot = await this.resolve(actor, context);
    return this.spotOutbound.requestToSpot(spot, new SendChatMessageAtSpotReq(actor.actorId, request.text)).yield<SendChatMessageRes>();
  }
}

@zlinkSpotActorSendHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.setTypingReq })
class SetTypingHandler extends ConversationActorRoute
  implements ZLinkSpotActorSendHandler<SupportUserActor, SetTypingReq> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) handles: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) outbound: ZLinkSpotOutbound
  ) { super(handles, outbound); }

  async handle(actor: SupportUserActor, context: ZLinkSpotActorSendContext, request: SetTypingReq): Promise<void> {
    const spot = await this.resolve(actor, context);
    await this.spotOutbound.sendToSpot(spot, new SetTypingAtSpotMsg(actor.actorId, request.isTyping)).submit();
  }
}

@zlinkSpotActorRequestHandler({ actor: () => SupportUserActor, spot: () => ConversationSpot, packetName: PacketNames.closeConversationReq })
class CloseConversationHandler extends ConversationActorRoute
  implements ZLinkSpotActorRequestHandler<SupportUserActor, CloseConversationReq, CloseConversationRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER) handles: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_OUTBOUND) outbound: ZLinkSpotOutbound,
    private readonly availability: AgentAvailabilityDirectory
  ) { super(handles, outbound); }

  async handle(actor: SupportUserActor, context: ZLinkSpotActorRequestContext): Promise<CloseConversationRes> {
    const spot = await this.resolve(actor, context);
    const response = await this.spotOutbound
      .requestToSpot(spot, new CloseConversationAtSpotReq(actor.actorId))
      .yield<CloseConversationRes>();
    if (response.state.agentActorId !== undefined) this.availability.released(response.state.agentActorId);
    return response;
  }
}

export { JoinConversationHandler, SendChatMessageHandler, SetTypingHandler, CloseConversationHandler };
