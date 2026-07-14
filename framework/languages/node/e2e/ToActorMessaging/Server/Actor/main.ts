import fs from 'node:fs';
import path from 'node:path';
import { Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import {
  ZLinkMessageFlowLogMode,
  ZLinkMessage,
  type ZLinkActor,
  type ZLinkActorContext,
  type ZLinkActorFactory,
  type ZLinkEntrySpot,
  type ZLinkEntrySpotContext,
  type ZLinkSpotActorSendContext,
  type ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';
import {
  ZLINK_ACTOR_MANAGER,
  ZLinkModule,
  zlinkEntrySpotActorRequestHandler,
  zlinkEntrySpotActorSendHandler,
  zlinkFramework
} from '@zlink-systems/nestjs';
import type { ZLinkActorManager } from '@zlink-systems/framework';
import { createRedisLocationStore, locationMessagingOptions } from '../../Shared/location-store';
import {
  PacketNames,
  type ActorAsk,
  type ActorNotify,
  ActorPushNotify,
  type ActorPushReq,
  type ActorRefSnapshot,
  type ActorReply
} from '../../Shared/messages';
import { EvidenceStore } from './evidence-store';
import { closeHttpServer, startHttpServer } from '../Support/http-server';
import { TO_ACTOR_OPTIONS, createToActorConfigurationModule } from '../../configuration';
import type { ServerOptions } from '../../configuration';

let options: ServerOptions;
let evidence: EvidenceStore;
let stopping = false;

class TestActor implements ZLinkActor {
  constructor(
    readonly actorId: string,
    readonly context: ZLinkActorContext
  ) {}
}

class TestActorFactory implements ZLinkActorFactory {
  async create(actorId: string, context: ZLinkActorContext): Promise<TestActor> {
    return new TestActor(actorId, context);
  }
}

class TestEntrySpot implements ZLinkEntrySpot<TestActor> {
  private readonly actors = new Map<string, TestActor>();

  constructor(readonly context: ZLinkEntrySpotContext) {}

  async onCreateActor(actor: TestActor, _request: ZLinkMessage): Promise<void> {
    this.actors.set(actor.actorId, actor);
    evidence.append({ scenario: 'create', actorId: actor.actorId, kind: 'create', value: 'created' });
  }

  async onActorJoin(actorId: string, _request: ZLinkMessage): Promise<{ accepted: boolean }> {
    evidence.append({ scenario: 'join', actorId, kind: 'join', value: 'joined' });
    return { accepted: true };
  }

  async onJoinedActor(_actor: TestActor): Promise<void> {}

  async onLeaveActor(_actor: TestActor): Promise<void> {}

  async destroy(actorId: string): Promise<void> {
    const actor = this.actors.get(actorId);
    if (actor === undefined) {
      throw new Error(`Actor '${actorId}' is not active.`);
    }
    await this.context.destroyActor(actor);
    this.actors.delete(actorId);
  }
}

@zlinkEntrySpotActorSendHandler({
  actor: () => TestActor,
  entrySpot: () => TestEntrySpot,
  packetName: PacketNames.actorNotify
})
class NotifyHandler {
  async handle(_spot: TestEntrySpot, actor: TestActor, _context: ZLinkSpotActorSendContext, message: ActorNotify): Promise<void> {
    evidence.append({ scenario: message.scenario, actorId: actor.actorId, kind: 'send', value: message.value });
  }
}

@zlinkEntrySpotActorRequestHandler({
  actor: () => TestActor,
  entrySpot: () => TestEntrySpot,
  packetName: PacketNames.actorAsk
})
class AskHandler {
  async handle(_spot: TestEntrySpot, actor: TestActor, _context: ZLinkSpotActorRequestContext, request: ActorAsk): Promise<ActorReply> {
    if (request.value === 'throw') {
      throw new Error('to-actor handler exception');
    }
    evidence.append({ scenario: request.scenario, actorId: actor.actorId, kind: 'request', value: request.value });
    return { scenario: request.scenario, actorId: actor.actorId, value: `reply:${request.value}` };
  }
}

@zlinkEntrySpotActorRequestHandler({
  actor: () => TestActor,
  entrySpot: () => TestEntrySpot,
  packetName: PacketNames.actorPush
})
class PushHandler {
  async handle(_spot: TestEntrySpot, actor: TestActor, _context: ZLinkSpotActorRequestContext, request: ActorPushReq): Promise<ActorReply> {
    await actor.context.boundSession
      .send(new ActorPushNotify(request.scenario, actor.actorId, request.value))
      .submit();
    evidence.append({ scenario: request.scenario, actorId: actor.actorId, kind: 'push', value: request.value });
    return { scenario: request.scenario, actorId: actor.actorId, value: `pushed:${request.value}` };
  }
}

class ActorModule {}
const configuration = createToActorConfigurationModule();
Module({
  imports: [
    configuration,
    ZLinkModule.forRootFactory({
      imports: [configuration],
      inject: [TO_ACTOR_OPTIONS],
      useFactory: (value: unknown) => {
        options = value as ServerOptions;
        fs.mkdirSync(options.logDir, { recursive: true });
        evidence = new EvidenceStore(options.evidenceFile ?? path.join(options.logDir, 'actor.evidence.log'));
        const builder = zlinkFramework();
        builder
          .addLocationStore(createRedisLocationStore({
            redisEndpoint: options.redisEndpoint,
            redisKeyPrefix: options.redisKeyPrefix
          }));
        Object.assign(builder.configureLocations(), locationMessagingOptions());
        builder
          .configureDispatch()
          .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
          .traceLogFile(path.join(options.logDir, 'actor-flow.log'))
          .traceLabel(options.rid);
        builder
          .addSpotMesh('to-actor')
          .enableRouter(options.routerEndpoint, options.rid)
          .configureEntrySpot({ routingId: options.rid })
          .enablePubSub(options.pubSubEndpoint, options.rid)
          .addEntrySpot(TestEntrySpot)
          .actorFactory('test-actor', TestActorFactory);
        return builder.build();
      }
    })
  ],
  providers: [TestActorFactory, TestEntrySpot, NotifyHandler, AskHandler, PushHandler]
})(ActorModule);

async function main(): Promise<void> {
  const app = await NestFactory.createApplicationContext(ActorModule, { logger: false, abortOnError: false });
  const actors = app.get(ZLINK_ACTOR_MANAGER, { strict: false }) as ZLinkActorManager;
  const entrySpot = app.get(TestEntrySpot, { strict: false });
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ok' }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.all() },
    {
      method: 'POST',
      path: '/actors/ta-a1/ensure',
      handle: async () => {
        const actor = await actors.getOrCreate('ta-a1', 'test-actor');
        return { actorId: 'ta-a1', actor: actorSnapshot(actor) };
      }
    },
    {
      method: 'POST',
      path: '/actors/ta-a2/ensure',
      handle: async () => {
        const actor = await actors.getOrCreate('ta-a2', 'test-actor');
        return { actorId: 'ta-a2', actor: actorSnapshot(actor) };
      }
    },
    {
      method: 'POST',
      path: '/actors/ta-a3/ensure',
      handle: async () => {
        const actor = await actors.getOrCreate('ta-a3', 'test-actor');
        return { actorId: 'ta-a3', actor: actorSnapshot(actor) };
      }
    },
    {
      method: 'POST',
      path: '/actors/ta-a4/ensure',
      handle: async () => {
        const actor = await actors.getOrCreate('ta-a4', 'test-actor');
        return { actorId: 'ta-a4', actor: actorSnapshot(actor) };
      }
    },
    {
      method: 'POST',
      path: '/actors/ta-b1-reference/ensure',
      handle: async () => {
        const actor = await actors.getOrCreate('ta-b1-reference', 'test-actor');
        return { actorId: 'ta-b1-reference', actor: actorSnapshot(actor) };
      }
    },
    {
      method: 'POST',
      path: '/actors/ta-b1-reference/destroy',
      handle: async () => {
        await entrySpot.destroy('ta-b1-reference');
        return { actorId: 'ta-b1-reference', status: 'destroyed' };
      }
    },
    {
      method: 'POST',
      path: '/actors/ta-b2/ensure',
      handle: async () => {
        const actor = await actors.getOrCreate('ta-b2', 'test-actor');
        return { actorId: 'ta-b2', actor: actorSnapshot(actor) };
      }
    },
    {
      method: 'POST',
      path: '/actors/ta-b3/ensure',
      handle: async () => {
        const actor = await actors.getOrCreate('ta-b3', 'test-actor');
        return { actorId: 'ta-b3', actor: actorSnapshot(actor) };
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

function actorSnapshot(actor: { readonly nodeRid: unknown; readonly actorId: string; readonly generation: bigint }): ActorRefSnapshot {
  return {
    nodeRid: String(actor.nodeRid),
    actorId: actor.actorId,
    generation: actor.generation.toString()
  };
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
