import { PacketNames } from '../../../../../../../Shared/Contracts/messages';

class CloseConversationHandler {
  packetName(): string {
    return PacketNames.closeConversationReq;
  }
}

export { CloseConversationHandler };
