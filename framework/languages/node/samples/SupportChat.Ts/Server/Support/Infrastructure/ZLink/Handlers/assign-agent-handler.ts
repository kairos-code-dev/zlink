import { Inject } from '@nestjs/common';
import { ZLINK_ACTOR_GATEWAY, ZLINK_ACTOR_MANAGER, zlinkRequestHandler } from '@zlink-systems/nestjs';
import { AgentAssignmentService } from '../../../Application/ConversationAssignment/agent-assignment-service';
import { SampleNames } from '../../../../Configuration/sample-names';
import { ConversationStatuses, PacketNames, SupportChatRoles, assignAgentRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkActorGateway, ZLinkActorManager, ZLinkRequestHandler } from '@zlink-systems/framework';
import type { AgentAssignmentService as AgentAssignmentServiceType } from '../../../Application/ConversationAssignment/agent-assignment-service';
import type {
  AssignAgentReq,
  AssignAgentRes,
  JoinConversationRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.assignAgentReq)
class AssignAgentHandler implements ZLinkRequestHandler<AssignAgentReq, AssignAgentRes> {
  constructor(
    @Inject(AgentAssignmentService) private readonly assignment: AgentAssignmentServiceType,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actorManager: ZLinkActorManager,
    @Inject(ZLINK_ACTOR_GATEWAY) private readonly actorGateway: ZLinkActorGateway
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
    const joined = await this.actorGateway
      .joinSpot(actorRef, request.conversationId)
      .submit<Partial<JoinConversationRes & { error: string }>>();
    if (joined.resultCode !== 0) {
      throw new Error(joined.reply?.error ?? `Conversation '${request.conversationId}' rejected agent '${assigned.actorId}'.`);
    }
    const state = (joined.reply as JoinConversationRes).state;
    return assignAgentRes(request.conversationId, state.status, assigned.actorId);
  }
}

export { AssignAgentHandler };
