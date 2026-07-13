import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_HANDLE_RESOLVER,
  ZLINK_SPOT_MANAGER,
  ZLINK_SPOT_OUTBOUND,
  zlinkRequestHandler
} from '@zlink-systems/nestjs';
import { joinConversation, PacketNames, SupportChatRoles } from '../../../../../Shared/Contracts/messages';
import { SupportConversationAllocator } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import { SupportActorDirectory } from '../Actors/support-actor-directory';
import { ConversationSpot } from '../Spots/ConversationSpot/conversation-spot';
import type { ZLinkRequestHandler, ZLinkSpotManager } from '@zlink-systems/framework';
import type { AllocateConversationReq, AllocateConversationRes } from '../../../../../Shared/Contracts/messages';
import type { ConversationCreateRequest } from '../Spots/ConversationSpot/conversation-create-request';
import { EnsureConversationAssignmentReq } from '../Spots/ConversationSpot/ConversationContracts';
import type { ZLinkSpotHandleResolver, ZLinkSpotOutbound } from '@zlink-systems/framework';

@zlinkRequestHandler('support', PacketNames.allocateConversationReq)
class AllocateConversationHandler implements ZLinkRequestHandler<AllocateConversationReq, AllocateConversationRes> {
  constructor(
    private readonly allocator: SupportConversationAllocator,
    private readonly actors: SupportActorDirectory,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly spotHandles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly spotOutbound: ZLinkSpotOutbound
  ) {}

  async handle(request: AllocateConversationReq): Promise<AllocateConversationRes> {
    const conversation = this.allocator.allocate(request.customerActorId, request.customerDisplayName, request.subject);
    const initial = conversation.snapshot();
    await this.spots.getOrCreate(ConversationSpot, initial.conversationId, {
      conversationId: initial.conversationId,
      customerActorId: initial.customerActorId,
      customerDisplayName: request.customerDisplayName,
      subject: initial.subject
    } satisfies ConversationCreateRequest);
    const customer = this.actors.require(request.customerActorId);
    const joined = await customer.context.joinSpot(
      initial.conversationId,
      joinConversation(customer.participantId, SupportChatRoles.Customer, customer.displayName)
    ).submit<{ state: typeof initial }>();
    if (joined.status !== 'accepted') {
      throw new Error(`Conversation '${initial.conversationId}' rejected customer actor.`);
    }
    const spotHandle = await this.spotHandles.resolveSpotHandle(initial.conversationId);
    if (spotHandle === undefined) throw new Error(`Conversation '${initial.conversationId}' was not resolved after customer join.`);
    const assigned = await this.spotOutbound
      .requestToSpot(spotHandle, new EnsureConversationAssignmentReq())
      .submit<{ state: typeof initial }>();
    const state = assigned.state;
    return { conversationId: state.conversationId, status: state.status };
  }
}

export { AllocateConversationHandler };
