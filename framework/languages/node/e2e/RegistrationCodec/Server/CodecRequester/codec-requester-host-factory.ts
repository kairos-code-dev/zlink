import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { zlinkProtobufCodec } from '@zlink-systems/framework-codec-protobuf/framework';
import { ZLINK_CHANNEL_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { RegistrationCodecNames } from '../../Shared/messages';
import { parseCodecRequesterOptions, type CodecRequesterOptions } from './Configuration/codec-requester-options';
import { createCodecRequesterEndpoints } from './Endpoints/codec-requester-endpoints';
import { createOperationalEndpoints } from './Endpoints/operational-endpoints';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startCodecRequester(args: readonly string[]): Promise<void> {
  const options = parseCodecRequesterOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  let stopping = false;
  const RequesterModule = createCodecRequesterModule(options);
  const app = await NestFactory.createApplicationContext(RequesterModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(options.httpUrl, [
    ...createOperationalEndpoints('codec-requester', options.rid, () => { stopping = true; }),
    ...createCodecRequesterEndpoints(channel)
  ]);
  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createCodecRequesterModule(options: CodecRequesterOptions): Function {
  class CodecRequesterModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .codecs()
              .use(zlinkProtobufCodec())
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.rid}-flow.log`)
              .traceLabel(options.rid);
          builder.addClientServerChannel(RegistrationCodecNames.channel)
            .enableClient(options.targetEndpoint);
          return builder.build();
        }
      })
    ]
  })(CodecRequesterModule);
  return CodecRequesterModule;
}
