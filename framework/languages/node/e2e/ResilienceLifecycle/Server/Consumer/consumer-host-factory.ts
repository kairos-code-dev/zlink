import fs from 'node:fs';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkMessageFlowLogMode } from '@zlink-systems/framework';
import { ZLINK_CHANNEL_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import type { ZLinkChannelClient } from '@zlink-systems/framework';
import type { ProfileRes, ProfileReq } from '../../Shared/messages';
import { parseConsumerOptions } from './Configuration/consumer-options';
import type { ConsumerOptions } from './Configuration/consumer-options';
import { createConsumerEndpoints, requestProfile } from './Endpoints/consumer-endpoints';
import { closeHttpServer, startHttpServer } from './Support/http-server';
import { createRedisLocationStore, resilienceLocationOptions } from '../../Shared/location-store';

export async function startConsumerHost(args: readonly string[]): Promise<void> {
  const options = parseConsumerOptions(args);
  fs.mkdirSync(options.logDir, { recursive: true });
  let stopping = false;
  const ConsumerModule = createConsumerModule(options);
  const app = await NestFactory.createApplicationContext(ConsumerModule, { logger: false, abortOnError: false });
  const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
  const server = await startHttpServer(
    options.httpUrl,
    createConsumerEndpoints(channel, (request) => requestWithNewClient(options, request), () => { stopping = true; })
  );

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

async function requestWithNewClient(options: ConsumerOptions, request: ProfileReq): Promise<ProfileRes> {
  const traceLabel = `storm-${request.marker ?? 'request'}`;
  const ConsumerModule = createConsumerModule(options, traceLabel);
  const app = await NestFactory.createApplicationContext(ConsumerModule, { logger: false, abortOnError: false });
  try {
    const channel = app.get(ZLINK_CHANNEL_CLIENT, { strict: false }) as ZLinkChannelClient;
    return await requestProfile(channel, request, 5000);
  } finally {
    await Promise.race([
      app.close(),
      new Promise<void>((resolve) => setTimeout(resolve, 5000))
    ]);
  }
}

function createConsumerModule(options: ConsumerOptions, traceLabel = options.traceLabel): Function {
  class ConsumerModule {}
  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => {
          const builder = zlinkFramework();
          builder
            .configureDispatch()
              .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
              .traceLogFile(`${options.logDir}/${traceLabel}-flow.log`)
              .traceLabel(traceLabel);

          const profile = builder.addClientServerChannel('profile');
          if (options.redisEndpoint !== undefined && options.redisKeyPrefix !== undefined) {
            builder.addLocationStore(createRedisLocationStore({
              redisEndpoint: options.redisEndpoint,
              redisKeyPrefix: options.redisKeyPrefix
            }));
            Object.assign(builder.configureLocations(), resilienceLocationOptions());
            profile.enableClient();
          } else {
            profile.enableClient(options.providerEndpoints);
          }
          return builder.build();
        }
      })
    ]
  })(ConsumerModule);
  return ConsumerModule;
}
