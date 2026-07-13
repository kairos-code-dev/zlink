import path from 'node:path';
import { Injectable, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import {
  ZLinkMessageFlowLogMode,
  type ActorRef,
  type ZLinkMessage,
  type ZLinkSession,
  type ZLinkSessionContext,
  type ZLinkSessionDispatchContext,
  type ZLinkSessionFactory
} from '@zlink-systems/framework';
import { ZLinkRedisLocationStore } from '@zlink-systems/framework-locations-redis';
import { ZLinkModule, zlinkFramework } from '@zlink-systems/nestjs';
import {
  SpotActorTransferNames,
  type BindActorSessionReq,
  type BindActorSessionRes
} from '../../Shared/messages';
import { closeHttpServer, startHttpServer } from '../Support/http-server';
import { EvidenceStore } from '../Support/evidence-store';

const options = parseOptions(process.argv.slice(2));
const evidence = new EvidenceStore(options.rid, options.evidenceFile);
let stopping = false;
process.once('SIGINT', () => { stopping = true; });
process.once('SIGTERM', () => { stopping = true; });

class GatewaySession implements ZLinkSession {
  constructor(readonly context: ZLinkSessionContext) {}

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    if (dispatch.packetName === SpotActorTransferNames.packetBindActor) {
      const request = payload.decode<BindActorSessionReq>(Object as never);
      if (request.nodeRid === undefined || request.generation === undefined) {
        throw new Error('Session gateway bind requires an ActorRef snapshot.');
      }
      const actor = {
        actorId: request.actorId,
        nodeRid: request.nodeRid,
        generation: BigInt(request.generation)
      } as ActorRef;
      evidence.correlate(request.actorId, request.transferId);
      await this.context.actors.bindOrGet(actor, signal);
      evidence.add(
        request.scenario,
        request.actorId,
        'session_bound',
        `gateway=${options.rid}|node=${String(actor.nodeRid)}|generation=${actor.generation}`
      );
      this.context.client.reply({
        scenario: request.scenario,
        actorId: actor.actorId,
        nodeRid: String(actor.nodeRid),
        generation: actor.generation.toString()
      } satisfies BindActorSessionRes).submit();
      return;
    }
    const actor = this.context.actors.bound[0];
    if (actor === undefined) throw new Error('No actor is bound.');
    await actor.relay(payload, signal);
  }
}

@Injectable()
class GatewaySessionFactory implements ZLinkSessionFactory<GatewaySession> {
  async create(context: ZLinkSessionContext): Promise<GatewaySession> { return new GatewaySession(context); }
}

class SessionModule {}
Module({
  imports: [
    ZLinkModule.forRootFactory({
      useFactory: () => {
        const builder = zlinkFramework();
        builder.addLocationStore(new ZLinkRedisLocationStore({
          url: `redis://${options.redisEndpoint}`,
          keyPrefix: options.redisKeyPrefix
        }));
        Object.assign(builder.configureLocations(), {
          pollingIntervalMs: 100,
          heartbeatIntervalMs: 1000,
          ownerLeaseTtlMs: 3000
        });
        builder.configureDispatch()
          .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
          .traceLogFile(path.join(options.logDir, `${options.rid}-flow.log`))
          .traceLabel(options.rid);
        builder.addSpotMesh(SpotActorTransferNames.mesh)
          .enableRouter(options.routerEndpoint, options.rid)
          .configureEntrySpot({ routingId: options.rid })
          .enablePubSub(options.pubSubEndpoint, options.rid);
        builder.addStreamNode(`${SpotActorTransferNames.mesh}-${options.rid}`)
          .bind(options.streamEndpoint)
          .registerSession(GatewaySessionFactory);
        return builder.build();
      }
    })
  ],
  providers: [GatewaySessionFactory]
})(SessionModule);

async function main(): Promise<void> {
  const app = await NestFactory.createApplicationContext(SessionModule, { logger: false, abortOnError: false });
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ok', rid: options.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    { method: 'POST', path: '/shutdown', handle: () => { stopping = true; return { status: 'stopping' }; } }
  ]);
  while (!stopping) await new Promise((resolve) => setTimeout(resolve, 100));
  await closeHttpServer(server);
  await app.close();
}

function parseOptions(args: readonly string[]): {
  rid: string;
  httpUrl: string;
  redisEndpoint: string;
  redisKeyPrefix: string;
  routerEndpoint: string;
  pubSubEndpoint: string;
  streamEndpoint: string;
  evidenceFile: string;
  logDir: string;
} {
  const values = new Map<string, string>();
  for (let index = 0; index < args.length; index += 2) {
    const value = args[index + 1];
    if (value === undefined) throw new Error(`Missing value for '${args[index]}'.`);
    values.set(args[index].replace(/^--/, ''), value);
  }
  const get = (key: string): string => {
    const value = values.get(key);
    if (value === undefined) throw new Error(`--${key} is required.`);
    return value;
  };
  return {
    rid: get('rid'),
    httpUrl: get('http-url'),
    redisEndpoint: get('redis-endpoint'),
    redisKeyPrefix: get('redis-key-prefix'),
    routerEndpoint: get('router-endpoint'),
    pubSubEndpoint: get('pubsub-endpoint'),
    streamEndpoint: get('stream-endpoint'),
    evidenceFile: get('evidence-file'),
    logDir: get('log-dir')
  };
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
