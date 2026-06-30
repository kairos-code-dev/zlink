import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { PacketNames, RegistrationCodecNames, type CodecScenarioResult } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createProtobufEndpoints(channel: ZLinkChannelClient, evidence: EvidenceStore): readonly HttpRoute[] {
  return [
    {
      method: 'POST',
      path: '/codec/protobuf',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-b2' })
          .packetName(PacketNames.echoProtobuf)
          .submit<CodecScenarioResult>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-b2', value: 'rc-b2-send' })
          .packetName(PacketNames.echoProtobufCommand)
          .submit();
        evidence.add(`codec-reply|codec=protobuf|value=${reply.value}|content=${reply.contentType}`);
        return reply;
      }
    }
  ];
}
