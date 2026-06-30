import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf';
import { ZLINK_CHANNEL_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames, RegistrationCodecNames } from '../../Shared/messages';
import { parseProtobufOptions, type ProtobufOptions } from './Configuration/protobuf-options';
import { createOperationalEndpoints } from './Endpoints/operational-endpoints';
import { createProtobufEndpoints } from './Endpoints/protobuf-endpoints';
import { ProtobufEchoCommandHandler, ProtobufEchoRequestHandler } from './Handlers/protobuf-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startProtobufPeer(args: readonly string[]): Promise<void> {
  const options = parseProtobufOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;
  const PeerModule = createPeerModule(options, evidence);
  const app = await NestFactory.createApplicationContext(PeerModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(options.httpUrl, [
    ...createOperationalEndpoints(evidence, () => { stopping = true; }),
    ...createProtobufEndpoints(channel, evidence)
  ]);
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createPeerModule(options: ProtobufOptions, evidence: EvidenceStore): Function {
  class ProtobufPeerModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
              .use(zlinkProtobufCodec())
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.addClientServerChannel(RegistrationCodecNames.channel)
            .enableServer(options.channelEndpoint)
            .enableClient(options.channelEndpoint)
            .addRequestHandler(PacketNames.echoProtobuf, ProtobufEchoRequestHandler)
            .addSendHandler(PacketNames.echoProtobufCommand, ProtobufEchoCommandHandler);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      ProtobufEchoRequestHandler,
      ProtobufEchoCommandHandler
    ]
  })(ProtobufPeerModule);
  return ProtobufPeerModule;
}
