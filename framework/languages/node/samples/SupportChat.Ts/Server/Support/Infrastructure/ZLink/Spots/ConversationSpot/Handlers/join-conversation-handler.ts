import { PacketNames } from '../../../../../../../Shared/Contracts/messages';

class JoinConversationHandler {
  packetName(): string {
    return PacketNames.joinConversationReq;
  }
}

export { JoinConversationHandler };
