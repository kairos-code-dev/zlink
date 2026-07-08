import fs from 'node:fs';
import path from 'node:path';
import { Injectable, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import {
  ZLinkMessageFlowLogMode,
  type ZLinkMessage,
  type ZLinkSession,
  type ZLinkSessionContext,
  type ZLinkSessionDispatchContext,
  type ZLinkSessionFactory
} from '@zlink-systems/framework';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import { createRedisLocationStore, locationMessagingOptions } from '../../Shared/location-store';
import { PacketNames, type ActorPushReq, type BindActorReq, type BindActorRes } from '../../Shared/messages';
import { closeHttpServer, startHttpServer } from '../Support/http-server';
import { parseServerOptions } from '../Support/options';

const options = parseServerOptions(process.argv.slice(2), 'session');
if (options.streamEndpoint === undefined) {
  throw new Error('--stream-endpoint is required for the session role.');
}
fs.mkdirSync(options.logDir, { recursive: true });
let stopping = false;

class ToActorSession implements ZLinkSession {
  constructor(readonly context: ZLinkSessionContext) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    if (dispatch.packetName === PacketNames.bindActor) {
      const request = payload.decode<BindActorReq>(Object as never);
      await this.context.actors.bindOrGet({
        actorId: request.actor.actorId,
        nodeRid: request.actor.nodeRid,
        generation: BigInt(request.actor.generation)
      }, signal);
      await this.context.client.reply({
        actorId: request.actor.actorId,
        nodeRid: request.actor.nodeRid,
        generation: request.actor.generation,
        boundCount: this.context.actors.bound.length
      } satisfies BindActorRes).submit(signal);
      return;
    }

    if (dispatch.packetName === PacketNames.actorPush) {
      const request = payload.decode<ActorPushReq>(Object as never);
      const actor = this.context.actors.find(request.actorId);
      if (actor === undefined) {
        throw new Error(`Actor route not found: ${request.actorId}`);
      }
      await actor.relay(payload, signal);
      return;
    }

    throw new Error(`Unsupported session packet '${dispatch.packetName}'.`);
  }
}

@Injectable()
class ToActorSessionFactory implements ZLinkSessionFactory<ToActorSession> {
  create(context: ZLinkSessionContext): ToActorSession {
    return new ToActorSession(context);
  }
}

class SessionModule {}
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
          .traceLogFile(path.join(options.logDir, 'session-flow.log'))
          .traceLabel(options.rid);
        builder
          .addSpotMesh('to-actor')
          .enableRouter(options.routerEndpoint, options.rid)
          .configureEntrySpot({ routingId: options.rid })
          .enablePubSub(options.pubSubEndpoint, options.rid);
        builder
          .addStreamNode('to-actor-session-stream')
          .bind(options.streamEndpoint)
          .registerSession(ToActorSessionFactory);
        return builder.build();
      }
    })
  ],
  providers: [ToActorSessionFactory]
})(SessionModule);

async function main(): Promise<void> {
  const app = await NestFactory.createApplicationContext(SessionModule, { logger: false, abortOnError: false });
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ok' }) },
    { method: 'POST', path: '/shutdown', handle: () => { stopping = true; return { status: 'stopping' }; } }
  ]);

  while (!stopping) {
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  await closeHttpServer(server);
  await app.close();
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
