import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import { parseConsumerOptions } from './Configuration/consumer-options';
import type { ConsumerOptions } from './Configuration/consumer-options';
import { createConsumerEndpoints } from './Endpoints/consumer-endpoints';
import { closeHttpServer, startHttpServer } from './Support/http-server';

export async function startConsumerHost(args: readonly string[]): Promise<void> {
  const options = parseConsumerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  let stopping = false;
  const ConsumerModule = createConsumerModule(options);
  const app = await NestFactory.createApplicationContext(ConsumerModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(options.httpUrl, createConsumerEndpoints(channel, () => { stopping = true; }));

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function createConsumerModule(options: ConsumerOptions): Function {
  class ConsumerModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${options.traceLabel}-flow.log`)
              .traceLabel(options.traceLabel);

          const profile = builder.addClientServerChannel('profile');
          if (options.registryRouterEndpoint !== undefined) {
            builder.useDiscovery().addRegistryEndpoint(options.registryRouterEndpoint);
            profile.enableClient();
          } else {
            profile.enableClient(options.providerEndpoints);
          }
          if (options.lowHighWaterMark) {
            const socket = profile.configureClientSocket();
            socket.sendHighWaterMark = 1;
            socket.receiveHighWaterMark = 1;
            socket.sendTimeoutMs = 100;
          }
          return builder.build();
        }
      })
    ]
  })(ConsumerModule);
  return ConsumerModule;
}
