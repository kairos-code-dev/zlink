import type { AssignAgentReq, AssignAgentRes } from '../../../../../Shared/Contracts/messages';

class AssignAgentHandler {
  handle(request: AssignAgentReq): AssignAgentRes {
    return {
      conversationId: request.conversationId,
      assigned: request.agentActorId.length > 0
    };
  }
}

export { AssignAgentHandler };
