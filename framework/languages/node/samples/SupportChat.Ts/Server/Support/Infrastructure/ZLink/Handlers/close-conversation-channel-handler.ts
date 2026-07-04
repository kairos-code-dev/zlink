import { PacketNames } from '../../../../../Shared/Contracts/messages';

class CloseConversationChannelHandler {
  packetName(): string {
    return PacketNames.closeConversationReq;
  }
}

export { CloseConversationChannelHandler };
