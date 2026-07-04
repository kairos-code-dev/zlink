import { PacketNames } from '../../../../../Shared/Contracts/messages';

class JoinConversationChannelHandler {
  packetName(): string {
    return PacketNames.joinConversationReq;
  }
}

export { JoinConversationChannelHandler };
