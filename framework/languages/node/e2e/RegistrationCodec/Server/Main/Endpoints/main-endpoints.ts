import type { ZLinkChannelClient } from '@zlink-systems/framework';
import {
  MessagePackEchoMsg,
  MessagePackEchoReq,
  PacketNames,
  ProtobufEchoMsg,
  ProtobufEchoReq,
  RegistrationCodecNames,
  type CodecScenarioRes,
  type EchoRes
} from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';
import type { HttpRoute } from '../Support/http-server';

export function createMainEndpoints(evidence: EvidenceStore, channel: ZLinkChannelClient): readonly HttpRoute[] {
  return [
    {
      method: 'POST',
      path: '/registration/auto',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a1' })
          .packetName(PacketNames.echoAutoReq)
          .submit<EchoRes>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-a1', value: 'rc-a1-send' })
          .packetName(PacketNames.echoAutoMsg)
          .submit();
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/registration/attribute',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a2' })
          .packetName(PacketNames.echoAttrReq)
          .submit<EchoRes>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-a2', value: 'rc-a2-send' })
          .packetName(PacketNames.echoAttrMsg)
          .submit();
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/registration/manual',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a3' })
          .packetName(PacketNames.echoManualReq)
          .submit<EchoRes>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-a3', value: 'rc-a3-send' })
          .packetName(PacketNames.echoManualMsg)
          .submit();
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/registration/di-filter-order',
      handle: async () => {
        const first = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a4-1' })
          .packetName(PacketNames.echoDiReq)
          .submit<EchoRes>();
        const second = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a4-2' })
          .packetName(PacketNames.echoDiReq)
          .submit<EchoRes>();
        return [first, second];
      }
    },
    {
      method: 'POST',
      path: '/registration/filter-order',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a5' })
          .packetName(PacketNames.echoManualReq)
          .submit<EchoRes>();
        evidence.add(`filter-reply|value=${reply.value}|content=${reply.contentType}`);
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/codec/json',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-b1' })
          .packetName(PacketNames.echoJsonReq)
          .submit<EchoRes>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-b1', value: 'rc-b1-send' })
          .packetName(PacketNames.echoJsonMsg)
          .submit();
        evidence.add(`codec-reply|codec=json|value=${reply.value}|content=${reply.contentType}`);
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/codec/roundtrip',
      handle: async () => {
        const json = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-b1' })
          .packetName(PacketNames.echoJsonReq)
          .submit<EchoRes>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-b1', value: 'rc-b1-send' })
          .packetName(PacketNames.echoJsonMsg)
          .submit();

        const protobuf = await channel.requestToChannel(RegistrationCodecNames.channel, new ProtobufEchoReq('rc-b2'))
          .packetName(PacketNames.echoProtobufReq)
          .submit<ProtobufEchoReq>();
        await channel.sendToChannel(RegistrationCodecNames.channel, new ProtobufEchoMsg('rc-b2-send'))
          .packetName(PacketNames.echoProtobufMsg)
          .submit();

        const messagePack = await channel.requestToChannel(RegistrationCodecNames.channel, new MessagePackEchoReq('rc-b3'))
          .packetName(PacketNames.echoMessagePackReq)
          .submit<MessagePackEchoReq>();
        await channel.sendToChannel(RegistrationCodecNames.channel, new MessagePackEchoMsg('cmd-rc-b3', 'rc-b3-send'))
          .packetName(PacketNames.echoMessagePackMsg)
          .submit();

        evidence.add(`codec-reply|codec=json|value=${json.value}|content=${json.contentType}`);
        evidence.add(`codec-reply|codec=protobuf|value=${protobuf.value}`);
        evidence.add(`codec-reply|codec=msgpack|value=${messagePack.value}`);
        return {
          json,
          protobufValue: protobuf.value,
          messagePackValue: messagePack.value
        } satisfies CodecScenarioRes;
      }
    }
  ];
}
