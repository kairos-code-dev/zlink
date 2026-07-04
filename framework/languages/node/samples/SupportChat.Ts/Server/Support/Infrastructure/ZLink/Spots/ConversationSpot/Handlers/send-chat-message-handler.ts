import { PacketNames } from '../../../../../../../Shared/Contracts/messages';

class SendChatMessageHandler {
  packetName(): string {
    return PacketNames.sendChatMessageReq;
  }
}

export { SendChatMessageHandler };
