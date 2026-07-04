import { PacketNames } from '../../../../../Shared/Contracts/messages';

class SetAgentAvailableChannelHandler {
  packetName(): string {
    return PacketNames.setAgentAvailableReq;
  }
}

export { SetAgentAvailableChannelHandler };
