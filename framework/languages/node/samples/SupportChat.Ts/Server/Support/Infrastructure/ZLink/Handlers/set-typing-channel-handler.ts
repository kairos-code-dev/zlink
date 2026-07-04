import { PacketNames } from '../../../../../Shared/Contracts/messages';

class SetTypingChannelHandler {
  packetName(): string {
    return PacketNames.setTypingReq;
  }
}

export { SetTypingChannelHandler };
