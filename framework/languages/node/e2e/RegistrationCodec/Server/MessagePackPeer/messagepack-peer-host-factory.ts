import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { zlinkMessagePackCodec } from '@zlink-systems/framework-codec-msgpack';
import { ZLINK_CHANNEL_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames, RegistrationCodecNames } from '../../Shared/messages';
import { parseMessagePackOptions, type MessagePackOptions } from './Configuration/messagepack-options';
import { createMessagePackEndpoints } from './Endpoints/messagepack-endpoints';
import { createOperationalEndpoints } from './Endpoints/operational-endpoints';
import { MessagePackEchoCommandHandler, MessagePackEchoRequestHandler } from './Handlers/messagepack-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startMessagePackPeer(args: readonly string[]): Promise<void> {
  const options = parseMessagePackOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;
  const PeerModule = createPeerModule(options, evidence);
  const app = await NestFactory.createApplicationContext(PeerModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(options.httpUrl, [
    ...createOperationalEndpoints(evidence, () => { stopping = true; }),
    ...createMessagePackEndpoints(channel, evidence)
  ]);
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createPeerModule(options: MessagePackOptions, evidence: EvidenceStore): Function {
  class MessagePackPeerModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
              .use(zlinkMessagePackCodec())
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.addClientServerChannel(RegistrationCodecNames.channel)
            .enableServer(options.channelEndpoint)
            .enableClient(options.channelEndpoint)
            .addRequestHandler(PacketNames.echoMessagePack, MessagePackEchoRequestHandler)
            .addSendHandler(PacketNames.echoMessagePackCommand, MessagePackEchoCommandHandler);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      MessagePackEchoRequestHandler,
      MessagePackEchoCommandHandler
    ]
  })(MessagePackPeerModule);
  return MessagePackPeerModule;
}
