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
import {
  SPOT_ACTOR_TRANSFER_OPTIONS,
  createSpotActorTransferConfigurationModule,
  validateServerOptions
} from '../../configuration';
import type { ServerOptions } from '../../configuration';

let options: ServerOptions;
let evidence: EvidenceStore;
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
const configuration = createSpotActorTransferConfigurationModule(
  SPOT_ACTOR_TRANSFER_OPTIONS,
  validateServerOptions
);
Module({
  imports: [
    configuration,
    ZLinkModule.forRootFactory({
      imports: [configuration],
      inject: [SPOT_ACTOR_TRANSFER_OPTIONS],
      useFactory: (value: unknown) => {
        options = value as ServerOptions;
        if (options.streamEndpoint === undefined) {
          throw new Error("Configuration value 'e2e.streamEndpoint' is required for the session host.");
        }
        evidence = new EvidenceStore(options.rid, options.evidenceFile);
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
        builder.addRouteMesh(SpotActorTransferNames.mesh)
          .listen(options.routerEndpoint).routingId(options.rid)
          .configureEntrySpot({ routingId: options.rid })
          .channelName(SpotActorTransferNames.mesh);
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

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
