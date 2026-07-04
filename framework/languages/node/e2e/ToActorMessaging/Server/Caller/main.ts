import fs from 'node:fs';
import path from 'node:path';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import { ZLinkFrameworkException, ZLinkMessageFlowLogMode, type ZLinkActorClient } from '@zlink-systems/framework';
import { ZLINK_ACTOR_CLIENT, ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { createRedisLocationStore, locationMessagingOptions } from '../../Shared/location-store';
import { actorAsk, actorNotify, PacketNames, type ActorCallRequest, type ActorCallResponse, type ActorReply } from '../../Shared/messages';
import { closeHttpServer, startHttpServer } from '../Support/http-server';
import { parseServerOptions } from '../Support/options';

const options = parseServerOptions(process.argv.slice(2), 'caller');
fs.mkdirSync(options.logDir, { recursive: true });
let stopping = false;

class CallerModule {}
Module({
  imports: [
    ZLinkModule.forRootFactory({
      useFactory: () => {
        const builder = zlinkFramework();
        builder.addLocationStore(createRedisLocationStore({
          redisEndpoint: options.redisEndpoint,
          redisKeyPrefix: options.redisKeyPrefix
        }));
        Object.assign(builder.configureLocations(), locationMessagingOptions());
        builder
          .configureDispatch()
          .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
          .traceLogFile(path.join(options.logDir, 'caller-flow.log'))
          .traceLabel(options.rid);
        builder
          .addSpotMesh('to-actor')
          .enableRouter(options.routerEndpoint, options.rid)
          .configureEntrySpot({ routingId: options.rid })
          .enablePubSub(options.pubSubEndpoint, options.rid);
        return builder.build();
      }
    })
  ]
})(CallerModule);

async function main(): Promise<void> {
  const app = await NestFactory.createApplicationContext(CallerModule, { logger: false, abortOnError: false });
  const actors = app.get(ZLINK_ACTOR_CLIENT, { strict: false }) as ZLinkActorClient;
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ok' }) },
    {
      method: 'POST',
      path: '/send',
      handle: async (body) => {
        const request = body as ActorCallRequest;
        try {
          await actors
            .sendToActor(request.actorId, actorNotify(request.scenario, request.actorId, request.value))
            .packetName(PacketNames.actorNotify)
            .submit();
          return { scenario: request.scenario, actorId: request.actorId, result: 'sent' } satisfies ActorCallResponse;
        } catch (error) {
          return failure(request, error);
        }
      }
    },
    {
      method: 'POST',
      path: '/request',
      handle: async (body) => {
        const request = body as ActorCallRequest;
        try {
          const reply = await actors
            .requestToActor(request.actorId, actorAsk(request.scenario, request.actorId, request.value))
            .packetName(PacketNames.actorAsk)
            .timeout(5000)
            .submit<ActorReply>();
          return { scenario: request.scenario, actorId: request.actorId, result: reply.value } satisfies ActorCallResponse;
        } catch (error) {
          return failure(request, error);
        }
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stopping = true; return { status: 'stopping' }; } }
  ]);

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

function failure(request: ActorCallRequest, error: unknown): ActorCallResponse {
  return {
    scenario: request.scenario,
    actorId: request.actorId,
    result: 'failed',
    errorKind: error instanceof ZLinkFrameworkException ? error.kind : error instanceof Error ? error.name : String(error)
  };
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
