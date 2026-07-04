import { PacketNames } from '../../../../../Shared/Contracts/messages';

class SendChatMessageChannelHandler {
  packetName(): string {
    return PacketNames.sendChatMessageReq;
  }
}

export { SendChatMessageChannelHandler };
