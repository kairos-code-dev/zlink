import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { AgentAssignmentService } from '../../../Application/ConversationAssignment/agent-assignment-service';
import { SupportUserActorFactory } from '../Actors/support-user-actor-factory';
import { ConversationStatuses, PacketNames, assignAgentRes } from '../../../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { AgentAssignmentService as AgentAssignmentServiceType } from '../../../Application/ConversationAssignment/agent-assignment-service';
import type { SupportUserActorFactory as SupportUserActorFactoryType } from '../Actors/support-user-actor-factory';
import type {
  AssignAgentReq,
  AssignAgentRes,
  JoinConversationRes
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('support', PacketNames.assignAgentReq)
class AssignAgentHandler implements ZLinkRequestHandler<AssignAgentReq, AssignAgentRes> {
  constructor(
    @Inject(AgentAssignmentService) private readonly assignment: AgentAssignmentServiceType,
    @Inject(SupportUserActorFactory) private readonly actors: SupportUserActorFactoryType
  ) {}

  async handle(request: AssignAgentReq): Promise<AssignAgentRes> {
    const assigned = this.assignment.assignNextAgent();
    if (assigned === null) {
      return assignAgentRes(request.conversationId, ConversationStatuses.WaitingForAgent, null);
    }

    const actor = this.actors.get(assigned.actorId);
    const joined = await actor.context.joinSpot(request.conversationId).submit<Partial<JoinConversationRes & { error: string }>>();
    if (joined.resultCode !== 0) {
      throw new Error(joined.reply?.error ?? `Conversation '${request.conversationId}' rejected agent '${actor.actorId}'.`);
    }
    const state = (joined.reply as JoinConversationRes).state;
    return assignAgentRes(request.conversationId, state.status, actor.actorId);
  }
}

export { AssignAgentHandler };
