import { Inject } from '@nestjs/common';
import {
  ZLINK_SPOT_MANAGER,
  zlinkRequestHandler
} from '@zlink-systems/nestjs';
import { ConversationStatuses, PacketNames } from '../../../../../Shared/Contracts/messages';
import { SampleNames } from '../../../../Configuration/sample-names';
import { SupportConversationAllocator } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import { ConversationSpot } from '../Spots/ConversationSpot/conversation-spot';
import type { ZLinkRequestHandler, ZLinkSpotManager } from '@zlink-systems/framework';
import type { AllocateConversationReq, AllocateConversationRes } from '../../../../../Shared/Contracts/messages';
import type { ConversationCreateRequest } from '../Spots/ConversationSpot/conversation-create-request';

@zlinkRequestHandler('support', PacketNames.allocateConversationReq)
class AllocateConversationHandler implements ZLinkRequestHandler<AllocateConversationReq, AllocateConversationRes> {
  constructor(
    private readonly allocator: SupportConversationAllocator,
    @Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager
  ) {}

  async handle(request: AllocateConversationReq): Promise<AllocateConversationRes> {
    const conversation = this.allocator.allocate(request.customerActorId, request.customerDisplayName, request.subject);
    const initial = conversation.snapshot();
    await this.spots.getOrCreate(SampleNames.conversationSpotMesh, ConversationSpot, initial.conversationId, {
      conversationId: initial.conversationId,
      customerActorId: initial.customerActorId,
      customerDisplayName: request.customerDisplayName,
      subject: initial.subject
    } satisfies ConversationCreateRequest);
    return {
      conversationId: initial.conversationId,
      status: ConversationStatuses.WaitingForAgent
    };
  }
}

export { AllocateConversationHandler };
