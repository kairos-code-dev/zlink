import type { ZLinkChannelClient } from '@zlink-systems/framework';
import {
  MessagePackEchoCommand,
  MessagePackEchoReq,
  PacketNames,
  ProtobufEchoCommand,
  ProtobufEchoReq,
  RegistrationCodecNames,
  type CodecScenarioResult,
  type EchoReply
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
          .packetName(PacketNames.echoAuto)
          .submit<EchoReply>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-a1', value: 'rc-a1-send' })
          .packetName(PacketNames.echoAutoCommand)
          .submit();
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/registration/attribute',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a2' })
          .packetName(PacketNames.echoAttr)
          .submit<EchoReply>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-a2', value: 'rc-a2-send' })
          .packetName(PacketNames.echoAttrCommand)
          .submit();
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/registration/manual',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a3' })
          .packetName(PacketNames.echoManual)
          .submit<EchoReply>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-a3', value: 'rc-a3-send' })
          .packetName(PacketNames.echoManualCommand)
          .submit();
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/registration/di-filter-order',
      handle: async () => {
        const first = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a4-1' })
          .packetName(PacketNames.echoDi)
          .submit<EchoReply>();
        const second = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a4-2' })
          .packetName(PacketNames.echoDi)
          .submit<EchoReply>();
        return [first, second];
      }
    },
    {
      method: 'POST',
      path: '/registration/filter-order',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-a5' })
          .packetName(PacketNames.echoManual)
          .submit<EchoReply>();
        evidence.add(`filter-reply|value=${reply.value}|content=${reply.contentType}`);
        return reply;
      }
    },
    {
      method: 'POST',
      path: '/codec/json',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-b1' })
          .packetName(PacketNames.echoJson)
          .submit<EchoReply>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-b1', value: 'rc-b1-send' })
          .packetName(PacketNames.echoJsonCommand)
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
          .packetName(PacketNames.echoJson)
          .submit<EchoReply>();
        await channel.sendToChannel(RegistrationCodecNames.channel, { commandId: 'cmd-rc-b1', value: 'rc-b1-send' })
          .packetName(PacketNames.echoJsonCommand)
          .submit();

        const protobuf = await channel.requestToChannel(RegistrationCodecNames.channel, new ProtobufEchoReq('rc-b2'))
          .packetName(PacketNames.echoProtobuf)
          .submit<ProtobufEchoReq>();
        await channel.sendToChannel(RegistrationCodecNames.channel, new ProtobufEchoCommand('rc-b2-send'))
          .packetName(PacketNames.echoProtobufCommand)
          .submit();

        const messagePack = await channel.requestToChannel(RegistrationCodecNames.channel, new MessagePackEchoReq('rc-b3'))
          .packetName(PacketNames.echoMessagePack)
          .submit<MessagePackEchoReq>();
        await channel.sendToChannel(RegistrationCodecNames.channel, new MessagePackEchoCommand('cmd-rc-b3', 'rc-b3-send'))
          .packetName(PacketNames.echoMessagePackCommand)
          .submit();

        evidence.add(`codec-reply|codec=json|value=${json.value}|content=${json.contentType}`);
        evidence.add(`codec-reply|codec=protobuf|value=${protobuf.value}`);
        evidence.add(`codec-reply|codec=msgpack|value=${messagePack.value}`);
        return {
          json,
          protobufValue: protobuf.value,
          messagePackValue: messagePack.value
        } satisfies CodecScenarioResult;
      }
    }
  ];
}
