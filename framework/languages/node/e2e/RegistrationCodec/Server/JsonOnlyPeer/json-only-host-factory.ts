import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { PacketNames, RegistrationCodecNames } from '../../Shared/messages';
import { parseJsonOnlyOptions, type JsonOnlyOptions } from './Configuration/json-only-options';
import { createOperationalEndpoints } from './Endpoints/operational-endpoints';
import { JsonOnlyEchoRequestHandler } from './Handlers/json-only-handlers';
import { EvidenceStore } from './Infrastructure/evidence-store';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startJsonOnlyPeer(args: readonly string[]): Promise<void> {
  const options = parseJsonOnlyOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  const evidence = new EvidenceStore(options.evidenceFile);
  let stopping = false;
  const PeerModule = createJsonOnlyModule(options, evidence);
  const app = await NestFactory.createApplicationContext(PeerModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(options.httpUrl, [
    ...createOperationalEndpoints(evidence, () => { stopping = true; }),
    {
      method: 'POST',
      path: '/codec/json-recovery',
      handle: async () => {
        const reply = await channel.requestToChannel(RegistrationCodecNames.channel, { value: 'rc-b5-json' })
          .packetName(PacketNames.echoJson)
          .submit();
        evidence.add('codec-mismatch-json-recovery|status=ok');
        return reply;
      }
    }
  ]);
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createJsonOnlyModule(options: JsonOnlyOptions, evidence: EvidenceStore): Function {
  class JsonOnlyModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.addClientServerChannel(RegistrationCodecNames.channel)
            .enableServer(options.channelEndpoint)
            .enableClient(options.channelEndpoint)
            .addRequestHandler(PacketNames.echoJson, JsonOnlyEchoRequestHandler);
          return builder.build();
        }
      })
    ],
    providers: [
      { provide: EvidenceStore, useValue: evidence },
      JsonOnlyEchoRequestHandler
    ]
  })(JsonOnlyModule);
  return JsonOnlyModule;
}
