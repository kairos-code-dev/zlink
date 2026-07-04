import { SampleNames } from '../../Configuration/sample-names';
import { PacketNames } from '../../../Shared/Contracts/messages';
import type { OpenConversationApiReq, OpenConversationApiRes } from '../../../Shared/Contracts/messages';

class OpenConversationHandler {
  async handle(request: OpenConversationApiReq): Promise<OpenConversationApiRes> {
    const supportChannel = SampleNames.supportChannel;
    const allocateConversationReq = PacketNames.allocateConversationReq;
    const assignAgentReq = PacketNames.assignAgentReq;
    void supportChannel;
    void allocateConversationReq;
    void assignAgentReq;
    return {
      conversationId: request.customerActorId,
      status: 'WaitingForAgent'
    };
  }
}

export { OpenConversationHandler };
