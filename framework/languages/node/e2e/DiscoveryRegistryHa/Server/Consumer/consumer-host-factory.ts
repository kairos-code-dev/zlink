import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode, type ZLinkLocationRuntimeQuery, type ZLinkChannelClient } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT, ZLINK_LOCATION_RUNTIME_QUERY, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { ChannelNames } from '../../Shared/messages';
import { createRedisLocationStore, storeFailureLocationOptions } from '../../Shared/location-store';
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
  const locationQuery = app.get(ZLINK_LOCATION_RUNTIME_QUERY, { strict: false }) as ZLinkLocationRuntimeQuery;
  const server = await startHttpServer(options.httpUrl, createConsumerEndpoints(channel, locationQuery, () => { stopping = true; }));
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
          builder.addLocationStore(createRedisLocationStore(options));
          Object.assign(builder.configureLocations(), storeFailureLocationOptions());
          builder.addClientServerChannel(ChannelNames.profile).enableClient();
          return builder.build();
        }
      })
    ]
  })(ConsumerModule);
  return ConsumerModule;
}
