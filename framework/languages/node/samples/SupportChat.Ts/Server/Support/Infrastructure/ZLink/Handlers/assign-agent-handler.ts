import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { AgentAssignmentService } from '../../../Application/ConversationAssignment/agent-assignment-service';
import { SupportConversationAllocator } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import { SampleNames } from '../../../../Configuration/sample-names';
import { ConversationStatuses, PacketNames, SupportChatRoles, assignAgentRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { AgentAssignmentService as AgentAssignmentServiceType } from '../../../Application/ConversationAssignment/agent-assignment-service';
import type { SupportConversationAllocator as SupportConversationAllocatorType } from '../../../Application/ConversationAssignment/support-conversation-allocator';
import type { AssignAgentReq, AssignAgentRes } from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.assignAgentReq)
class AssignAgentHandler implements ZLinkRequestHandler<AssignAgentReq, AssignAgentRes> {
  constructor(
    @Inject(AgentAssignmentService) private readonly assignment: AgentAssignmentServiceType,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(SupportConversationAllocator) private readonly conversations: SupportConversationAllocatorType
  ) {}

  async handle(request: AssignAgentReq): Promise<AssignAgentRes> {
    const assigned = this.assignment.assignNextAgent();
    if (assigned === null) {
      return assignAgentRes(request.conversationId, ConversationStatuses.WaitingForAgent, null);
    }

    const actorRef = await this.actorManager.getOrCreate(
      assigned.actorId,
      SampleNames.supportActorType,
      { actorId: assigned.actorId, displayName: assigned.displayName, role: SupportChatRoles.agent }
    );
    const state = await this.conversations.executeInConversation(
      request.conversationId,
      (conversation) => conversation.joinAgentByIdentity(actorRef.actorId, assigned.displayName)
    );
    return assignAgentRes(request.conversationId, state.status, assigned.actorId);
  }
}

export { AssignAgentHandler };
