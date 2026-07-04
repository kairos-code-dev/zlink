import { PacketNames } from '../../../../../Shared/Contracts/messages';
import type { AssignAgentReq, AssignAgentRes } from '../../../../../Shared/Contracts/messages';

class AssignAgentHandler {
  handle(request: AssignAgentReq): AssignAgentRes {
    return {
      conversationId: request.conversationId,
      assigned: request.packetName() === PacketNames.assignAgentReq
    };
  }
}

export { AssignAgentHandler };
