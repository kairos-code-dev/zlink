import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import {
  createMessagePackSerializer,
  ZLINK_MESSAGEPACK_CONTENT_TYPE
} from '@zlink-systems/framework-codec-msgpack/framework';
import {
  createProtobufMessageSerializer,
  ZLINK_PROTOBUF_CONTENT_TYPE
} from '@zlink-systems/framework-codec-protobuf/framework';
import { ZLINK_CHANNEL_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import {
  MessagePackEchoMsg,
  MessagePackEchoReq,
  PacketNames,
  ProtobufEchoMsg,
  ProtobufEchoReq,
  RegistrationCodecNames
} from '../../Shared/messages';
import { parseServerOptions, type ServerOptions } from './Configuration/server-options';
import { createMainEndpoints } from './Endpoints/main-endpoints';
import { createOperationalEndpoints } from './Endpoints/operational-endpoints';
import {
  JsonEchoCommandHandler,
  JsonEchoRequestHandler,
  MessagePackEchoCommandHandler,
  MessagePackEchoRequestHandler,
  ProtobufEchoCommandHandler,
  ProtobufEchoRequestHandler
} from './Handlers/codec-handlers';
import { DiEchoRequestHandler } from './Handlers/di-echo-handler';
import { FirstFilter, SecondFilter } from './Handlers/dispatch-filters';
import {
  DuplicateEchoRequestHandler,
  EchoAttrCommandHandler,
  EchoAttrRequestHandler,
  EchoAutoCommandHandler,
  EchoAutoRequestHandler,
  EchoManualCommandHandler,
  EchoManualRequestHandler
} from './Handlers/registration-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { ScopedProbe, SingletonProbe } from './Infrastructure/lifecycle-probes';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export interface MainHostOptions {
  readonly duplicate?: boolean;
}

export async function startMainHost(args: readonly string[], hostOptions: MainHostOptions = {}): Promise<void> {
  const options = parseServerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;

  const MainModule = createMainModule(options, evidence, hostOptions);
  const app = await NestFactory.createApplicationContext(MainModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(options.httpUrl, [
    ...createOperationalEndpoints(evidence, () => { stopping = true; }),
    ...createMainEndpoints(evidence, channel)
  ]);

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createMainModule(options: ServerOptions, evidence: EvidenceStore, hostOptions: MainHostOptions): Function {
  class MainModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .codecs()
              .use({
                register: (codecs) => {
                  codecs
                    .addSerializer(ZLINK_PROTOBUF_CONTENT_TYPE, {
                      ...createProtobufMessageSerializer(),
                      canSerialize: (value) => value instanceof ProtobufEchoReq || value instanceof ProtobufEchoMsg
                    })
                    .addSerializer(ZLINK_MESSAGEPACK_CONTENT_TYPE, {
                      ...createMessagePackSerializer(),
                      canSerialize: (value) => value instanceof MessagePackEchoReq || value instanceof MessagePackEchoMsg
                    });
                }
              })
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.options({ filters: [FirstFilter, SecondFilter] });
          const channel = builder.addClientServerChannel(RegistrationCodecNames.channel)
            .enableServer(options.channelEndpoint)
            .enableClient(options.channelEndpoint)
            .addHandlerGroup('auto')
            .addHandlerGroup('attr')
            .addRequestHandler(PacketNames.echoManualReq, EchoManualRequestHandler)
            .addSendHandler(PacketNames.echoManualMsg, EchoManualCommandHandler)
            .addRequestHandler(PacketNames.echoDiReq, DiEchoRequestHandler)
            .addRequestHandler(PacketNames.echoJsonReq, JsonEchoRequestHandler)
            .addSendHandler(PacketNames.echoJsonMsg, JsonEchoCommandHandler)
            .addRequestHandler(PacketNames.echoProtobufReq, ProtobufEchoRequestHandler)
            .addSendHandler(PacketNames.echoProtobufMsg, ProtobufEchoCommandHandler)
            .addRequestHandler(PacketNames.echoMessagePackReq, MessagePackEchoRequestHandler)
            .addSendHandler(PacketNames.echoMessagePackMsg, MessagePackEchoCommandHandler);
          if (hostOptions.duplicate === true) {
            channel.addRequestHandler(PacketNames.echoManualReq, DuplicateEchoRequestHandler);
          }
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      EchoAutoRequestHandler,
      EchoAutoCommandHandler,
      EchoAttrRequestHandler,
      EchoAttrCommandHandler,
      EchoManualRequestHandler,
      EchoManualCommandHandler,
      DuplicateEchoRequestHandler,
      DiEchoRequestHandler,
      JsonEchoRequestHandler,
      JsonEchoCommandHandler,
      ProtobufEchoRequestHandler,
      ProtobufEchoCommandHandler,
      MessagePackEchoRequestHandler,
      MessagePackEchoCommandHandler,
      SingletonProbe,
      ScopedProbe,
      FirstFilter,
      SecondFilter
    ]
  })(MainModule);
  return MainModule;
}
