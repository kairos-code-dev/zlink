import { PacketNames } from '../../../../../Shared/Contracts/messages';

class OpenConversationChannelHandler {
  packetName(): string {
    return PacketNames.openConversationReq;
  }
}

export { OpenConversationChannelHandler };
