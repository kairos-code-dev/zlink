import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { PacketNames, RegistrationCodecNames, type CodecScenarioRes } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createMessagePackEndpoints(channel: ZLinkChannelClient, evidence: EvidenceStore): readonly HttpRoute[] {
  return [
    {
      method: 'POST',
      path: '/codec/msgpack',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-b3' })
          .packetName(PacketNames.echoMessagePackReq)
          .submit<CodecScenarioRes>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-b3', value: 'rc-b3-send' })
          .packetName(PacketNames.echoMessagePackMsg)
          .submit();
        evidence.add(`codec-reply|codec=msgpack|value=${reply.value}|content=${reply.contentType}`);
        return reply;
      }
    }
  ];
}
