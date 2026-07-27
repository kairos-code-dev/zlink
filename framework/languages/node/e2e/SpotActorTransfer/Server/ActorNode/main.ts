import fs from 'node:fs';
import path from 'node:path';
import { Inject, Injectable, Module } from '@nestjs/common';
import { NestFactory } from '@nestjs/core';
import {
  ZLinkMessage,
  ZLinkEncodedPayload,
  ZLinkLocationActorEventKind,
  ZLinkMessageFlowLogMode,
  ZLinkSpotActorRequest,
  ZLinkSpotActorSend,
  zlinkRecreateRelocation,
  type ActorRef,
  type ZLinkActor,
  type ZLinkActorClient,
  type ZLinkActorContext,
  type ZLinkActorFactory,
  type ZLinkActorJoinCompletion,
  type ZLinkActorJoinRequest,
  type ZLinkActorMembership,
  type ZLinkActorManager,
  type ZLinkActorTransferAdapter,
  type ZLinkEntrySpot,
  type ZLinkEntrySpotActorRequestHandler,
  type ZLinkEntrySpotActorSendHandler,
  type ZLinkEntrySpotContext,
  type ZLinkLocationActorEvent,
  type ZLinkRuntimeEventHandler,
  type ZLinkRuntimeEvent,
  type ZLinkSpot,
  type ZLinkSpotActorRequestContext,
  type ZLinkSpotActorSendContext,
  type ZLinkSpotActorRequestHandler,
  type ZLinkSpotActorSendHandler,
  type ZLinkSpotContext,
  type ZLinkSpotManager
} from '@zlink-systems/framework';
import {
  ZLinkRedisLocationStore,
  ZLinkRedisRelocationStore
} from '@zlink-systems/framework-locations-redis';
import {
  ZLINK_ACTOR_CLIENT,
  ZLINK_ACTOR_MANAGER,
  ZLINK_SPOT_MANAGER,
  ZLinkModule,
  zlinkRuntimeEventHandler,
  zlinkFramework
} from '@zlink-systems/nestjs';
import {
  BoundPushNotify,
  BoundPushReq,
  HandoffProbe,
  JoinTargetReq,
  ProbeReq,
  SpotActorTransferNames,
  type ActorCreateReq,
  type ActorCreateRes,
  type ActorRefSnapshotRes,
  type BoundPushRes,
  type CreateSpotReq,
  type CreateSpotRes,
  type EvidenceWaitReq,
  type GateReleaseRes,
  type JoinTargetRes,
  type ProbeRes,
  type TransferStateDto
} from '../../Shared/messages';
import { EvidenceStore } from '../Support/evidence-store';
import { closeHttpServer, startHttpServer } from '../Support/http-server';
import {
  SPOT_ACTOR_TRANSFER_OPTIONS,
  createSpotActorTransferConfigurationModule,
  validateServerOptions
} from '../../configuration';
import type { ServerOptions } from '../../configuration';

let options: ServerOptions;
let evidence: EvidenceStore;
let domainState: DomainStateStore;
let joinedGates: GateStore;
let transferGates: GateStore;
let stopping = false;
process.once('SIGINT', () => { stopping = true; });
process.once('SIGTERM', () => { stopping = true; });
const actorScenarios = new Map<string, string>();
const capturedActorRefs = new Map<string, ActorRef>();
const actorLifecycleStates = new Map<string, { actorType: string; stateVersion: number }>();
const joinCompletions = new Map<string, ZLinkActorJoinCompletion>();

class ApplyActorLifecycleState {
  constructor(readonly actorType: string, readonly stateVersion: number) {}
}

class TransferActor implements ZLinkActor {
  actorType: string = SpotActorTransferNames.actorTypeStateful;
  stateVersion = 0;
  readonly context!: ZLinkActorContext;

  constructor(readonly actorId: string, context?: ZLinkActorContext) {
    if (context !== undefined) Object.defineProperty(this, 'context', { value: context, configurable: true });
  }

  configure(): void {
    this.context.handlers.addHandler(JoinTargetHandler);
    this.context.handlers.addHandler(ProbeHandler);
    this.context.handlers.addHandler(HandoffHandler);
    this.context.handlers.addHandler(BoundPushHandler);
    this.context.handlers.addHandler(ApplyActorLifecycleStateHandler);
  }

  async onJoinCompleted(completion: ZLinkActorJoinCompletion): Promise<void> {
    joinCompletions.set(this.actorId, completion);
    if (completion.status === 'accepted' && completion.reply !== undefined) {
      const reply = completion.reply.decode<JoinTargetRes>(Object as never);
      evidence.add(
        actorScenarios.get(this.actorId) ?? reply.scenario,
        this.actorId,
        'commit_ack',
        reply.targetSpotRid
      );
    }
    evidence.add(
      actorScenarios.get(this.actorId) ?? 'deferred-join',
      this.actorId,
      'join_completion',
      completion.status === 'failed'
        ? `${completion.status}|${completion.operationId.high}:${completion.operationId.low}|kind=${completion.kind}|retriable=${completion.isRetriable}`
        : `${completion.status}|${completion.operationId.high}:${completion.operationId.low}`
    );
  }
}

class ApplyActorLifecycleStateHandler {
  @ZLinkSpotActorSend('ApplyActorLifecycleState')
  async handle(
    actor: TransferActor,
    _context: ZLinkSpotActorSendContext,
    message: ApplyActorLifecycleState
  ): Promise<void> {
    actor.actorType = message.actorType;
    actor.stateVersion = message.stateVersion;
  }
}

class NoAdapterActor extends TransferActor {}

@Injectable()
class TransferActorFactory implements ZLinkActorFactory {
  async create(context: ZLinkActorContext): Promise<TransferActor> {
    const actorId = context.actorId;
    actorLifecycleStates.set(actorId, {
      actorType: SpotActorTransferNames.actorTypeStateful,
      stateVersion: 0
    });
    return new TransferActor(actorId, context);
  }
}

@Injectable()
class NoAdapterActorFactory implements ZLinkActorFactory {
  async create(context: ZLinkActorContext): Promise<NoAdapterActor> {
    const actorId = context.actorId;
    if (options.rid === 'actor-b') evidence.add('transfer', actorId, 'transfer_in_empty_default', 'actor-factory');
    const actor = new NoAdapterActor(actorId, context);
    actor.actorType = SpotActorTransferNames.actorTypeNoAdapter;
    actorLifecycleStates.set(actorId, { actorType: actor.actorType, stateVersion: actor.stateVersion });
    return actor;
  }
}

@Injectable()
class TransferActorAdapter implements ZLinkActorTransferAdapter<TransferActor> {
  async transferOut(actor: TransferActor, signal?: AbortSignal): Promise<ZLinkMessage> {
    signal?.throwIfAborted();
    if (actor.actorType === SpotActorTransferNames.actorTypeFailTransferOut) {
      evidence.add('ST-C3', actor.actorId, 'transfer_out_failed', String(actor.stateVersion));
      throw new Error('injected transfer out failure');
    }
    if (actor.actorType === SpotActorTransferNames.actorTypeEmptyState) {
      evidence.add('transfer', actor.actorId, 'transfer_out_empty', 'custom-adapter');
      return ZLinkMessage.fromEncoded(ZLinkEncodedPayload.from(Buffer.alloc(0)));
    }
    evidence.add('transfer', actor.actorId, 'transfer_out', String(actor.stateVersion));
    if (actor.actorId.startsWith('actor-handoff-gate-')) {
      const scenario = actorScenarios.get(actor.actorId) ?? 'ST-F1';
      evidence.add(scenario, actor.actorId, 'before_commit_gate', String(actor.stateVersion));
      await transferGates.wait(actor.actorId, signal);
    }
    actorLifecycleStates.set(actor.actorId, { actorType: actor.actorType, stateVersion: actor.stateVersion });
    return ZLinkMessage.from({
      actorId: actor.actorId,
      actorType: actor.actorType,
      stateVersion: actor.stateVersion
    } satisfies TransferStateDto);
  }

  async transferIn(actorId: string, state: ZLinkMessage, signal?: AbortSignal): Promise<TransferActor> {
    signal?.throwIfAborted();
    if (state.toEncodedPayload().isEmpty()) {
      evidence.add('transfer', actorId, 'transfer_in_empty', 'custom-adapter');
      const actor = new TransferActor(actorId);
      actor.actorType = SpotActorTransferNames.actorTypeEmptyState;
      return actor;
    }
    const dto = state.decode<TransferStateDto>(Object as never);
    if (actorId.startsWith('actor-fail-transfer-in-')) {
      evidence.add('ST-C3', actorId, 'transfer_in_failed', String(dto.stateVersion));
      throw new Error('injected transfer in failure');
    }
    const actor = new TransferActor(actorId);
    actor.actorType = dto.actorType;
    actor.stateVersion = dto.stateVersion;
    actorLifecycleStates.set(actorId, { actorType: actor.actorType, stateVersion: actor.stateVersion });
    evidence.add('transfer', actorId, 'transfer_in', String(actor.stateVersion));
    return actor;
  }
}

@Injectable()
class TransferEntrySpot implements ZLinkEntrySpot<TransferActor> {
  readonly context!: ZLinkEntrySpotContext<TransferActor>;

  constructor(@Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient) {}

  async onCreateActor(actor: ZLinkActorMembership, request: ZLinkMessage): Promise<{ accepted: boolean }> {
    let actorType = actor.actorType;
    let stateVersion = 0;
    if (!request.toEncodedPayload().isEmpty()) {
      const create = request.decode<ActorCreateReq>(Object as never);
      actorType = create.actorType;
      stateVersion = create.stateVersion;
      if (actorType === SpotActorTransferNames.actorTypeEmptyState) {
        domainState.save(actor.actor.actorId, stateVersion);
      }
    }
    actorLifecycleStates.set(actor.actor.actorId, { actorType, stateVersion });
    await this.actors
      .sendToActor(
        SpotActorTransferNames.mesh,
        actor.actor,
        new ApplyActorLifecycleState(actorType, stateVersion)
      )
      .submit();
    evidence.add('create', actor.actor.actorId, 'create', `${actorType}:${stateVersion}`);
    return { accepted: true };
  }

  async onActorJoin(actor: ZLinkActorJoinRequest, request: ZLinkMessage): Promise<{ accepted: boolean; reply?: unknown }> {
    const actorId = actor.actor.actorId;
    evidence.add('local', actorId, 'admission', 'actor-id-only');
    return { accepted: true, reply: request.decode(Object as never) };
  }

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    const state = actorLifecycleStates.get(actor.actor.actorId);
    evidence.add('local', actor.actor.actorId, 'entry_joined', String(state?.stateVersion ?? 0));
  }

  async onLeaveActor(actor: ZLinkActorMembership): Promise<void> {
    const actorId = actor.actor.actorId;
    const state = actorLifecycleStates.get(actorId);
    if (actor.actorType === SpotActorTransferNames.actorTypeNoAdapter) {
      evidence.add('transfer', actorId, 'transfer_out_empty_default', 'no-adapter');
    }
    if (actor.actorType === SpotActorTransferNames.actorTypeFailLeave) {
      evidence.add('ST-C3', actorId, 'leave_failed', String(state?.stateVersion ?? 0));
      throw new Error('injected source leave failure');
    }
    evidence.add('transfer', actorId, 'leave', String(state?.stateVersion ?? 0));
    const scenario = actorScenarios.get(actorId);
    if (scenario !== undefined) {
      // Returning from this callback lets the coordinator send the commit request.
      evidence.add(scenario, actorId, 'commit_request', 'after-source-leave');
    }
  }

  async onDisconnectActor(actor: ZLinkActorMembership): Promise<void> { void actor; }
}

@Injectable()
class TransferUserSpot implements ZLinkSpot<TransferActor> {
  readonly context!: ZLinkSpotContext<TransferActor>;
  private mode = 'accept';
  private readonly scenarios = new Map<string, string>();

  constructor(@Inject(ZLINK_ACTOR_CLIENT) private readonly actors: ZLinkActorClient) {}

  async onCreate(request: ZLinkMessage): Promise<{ accepted: boolean }> {
    if (!request.toEncodedPayload().isEmpty()) this.mode = request.decode<CreateSpotReq>(Object as never).mode ?? 'accept';
    evidence.add('create_spot', String(this.context.spotId), 'spot_created', this.mode);
    return { accepted: true };
  }

  async onActorJoin(actor: ZLinkActorJoinRequest, request: ZLinkMessage): Promise<{ accepted: boolean; reply: JoinTargetRes }> {
    const actorId = actor.actor.actorId;
    const join = request.decode<JoinTargetReq>(Object as never);
    evidence.correlate(actorId, join.transferId);
    this.scenarios.set(actorId, join.scenario);
    actorScenarios.set(actorId, join.scenario);
    evidence.add(join.scenario, actorId, 'admission', `spot=${this.context.spotId}|mode=${this.mode}|input=actor-id-only`);
    if (join.scenario === 'ST-C1') {
      await transferGates.wait(actorId);
    }
    const accepted = this.mode !== 'reject' && join.expectedMode !== 'reject';
    return {
      accepted,
      reply: {
        scenario: join.scenario,
        actorId,
        accepted,
        sourceNodeRid: '',
        targetSpotRid: String(this.context.spotId),
        stateVersion: 0
      }
    };
  }

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    const actorId = actor.actor.actorId;
    const scenario = this.scenarios.get(actorId) ?? 'transfer';
    if (this.mode === 'delay-joined') {
      evidence.add(scenario, actorId, 'joined_wait', String(this.context.spotId));
      await joinedGates.wait(String(this.context.spotId));
      evidence.add(scenario, actorId, 'joined_released', String(this.context.spotId));
    }
    if (this.mode === 'fail-joined') {
      evidence.add(scenario, actorId, 'joined_failed', String(this.context.spotId));
      throw new Error('injected joined failure');
    }
    const current = actorLifecycleStates.get(actorId) ?? { actorType: actor.actorType, stateVersion: 0 };
    evidence.add('transfer', actorId, 'joined', `${this.context.spotId}:${current.stateVersion}`);
    if (actor.actorType === SpotActorTransferNames.actorTypeEmptyState) {
      const stateVersion = domainState.load(actorId);
      actorLifecycleStates.set(actorId, { actorType: actor.actorType, stateVersion });
      await this.actors
        .sendToActor(
          SpotActorTransferNames.mesh,
          actor.actor,
          new ApplyActorLifecycleState(actor.actorType, stateVersion)
        )
        .submit();
      evidence.add('transfer', actorId, 'domain_state_loaded', actorId);
    }
  }

  async onLeaveActor(actor: ZLinkActorMembership): Promise<void> {
    evidence.add('transfer', actor.actor.actorId, 'target_leave', String(this.context.spotId));
  }

  async onDisconnectActor(actor: ZLinkActorMembership): Promise<void> { void actor; }
}

@Injectable()
class JoinTargetHandler {
  @ZLinkSpotActorRequest(SpotActorTransferNames.packetJoin)
  async handle(actor: TransferActor, _context: ZLinkSpotActorRequestContext, request: JoinTargetReq): Promise<JoinTargetRes> {
    evidence.correlate(actor.actorId, request.transferId);
    actorScenarios.set(actor.actorId, request.scenario);
    actor.context.joinSpot(request.targetSpotRid, request).timeout(10000).defer();
    evidence.add(request.scenario, actor.actorId, 'join_deferred', request.targetSpotRid);
    return {
      scenario: request.scenario,
      actorId: actor.actorId,
      accepted: true,
      sourceNodeRid: options.rid,
      targetSpotRid: request.targetSpotRid,
      stateVersion: actor.stateVersion
    };
  }
}

@Injectable()
class UserJoinTargetHandler implements ZLinkSpotActorRequestHandler<TransferActor, JoinTargetReq, JoinTargetRes> {
  @ZLinkSpotActorRequest(SpotActorTransferNames.packetJoin)
  async handle(actor: TransferActor, _context: ZLinkSpotActorRequestContext, request: JoinTargetReq): Promise<JoinTargetRes> {
    evidence.correlate(actor.actorId, request.transferId);
    actorScenarios.set(actor.actorId, request.scenario);
    actor.context.joinSpot(request.targetSpotRid, request).timeout(10000).defer();
    evidence.add(request.scenario, actor.actorId, 'join_deferred', request.targetSpotRid);
    return {
      scenario: request.scenario,
      actorId: actor.actorId,
      accepted: true,
      sourceNodeRid: options.rid,
      targetSpotRid: request.targetSpotRid,
      stateVersion: actor.stateVersion
    };
  }
}

@Injectable()
class ProbeHandler {
  @ZLinkSpotActorRequest(SpotActorTransferNames.packetProbe)
  async handle(actor: TransferActor, _context: ZLinkSpotActorRequestContext, request: ProbeReq): Promise<ProbeRes> {
    evidence.add(
      request.scenario,
      actor.actorId,
      actor.context.spotId === undefined ? 'entry_packet_handler' : 'packet_handler',
      request.marker
    );
    if (request.delayMs !== undefined) await delay(request.delayMs);
    const response = probeResponse(actorContextLocation(actor), actor, request);
    evidence.add(request.scenario, actor.actorId, 'request_reply', request.marker);
    return response;
  }
}

@Injectable()
class HandoffHandler {
  @ZLinkSpotActorSend(SpotActorTransferNames.packetHandoff)
  async handle(actor: TransferActor, _context: ZLinkSpotActorSendContext, message: HandoffProbe): Promise<void> {
    evidence.add(
      message.scenario,
      actor.actorId,
      actor.context.spotId === undefined ? 'entry_packet_handler' : 'packet_handler',
      message.marker
    );
  }
}

@Injectable()
class EntryHandoffHandler implements ZLinkEntrySpotActorSendHandler<TransferActor, HandoffProbe> {
  @ZLinkSpotActorSend(SpotActorTransferNames.packetHandoff)
  async handle(actor: TransferActor, _context: ZLinkSpotActorSendContext, message: HandoffProbe): Promise<void> {
    evidence.add(message.scenario, actor.actorId, 'entry_packet_handler', message.marker);
  }
}

@Injectable()
class EntryProbeHandler implements ZLinkEntrySpotActorRequestHandler<TransferActor, ProbeReq, ProbeRes> {
  @ZLinkSpotActorRequest(SpotActorTransferNames.packetProbe)
  async handle(actor: TransferActor, _context: ZLinkSpotActorRequestContext, request: ProbeReq): Promise<ProbeRes> {
    evidence.add(request.scenario, actor.actorId, 'entry_packet_handler', request.marker);
    if (request.delayMs !== undefined) await delay(request.delayMs);
    const response = probeResponse(actorContextLocation(actor), actor, request);
    evidence.add(request.scenario, actor.actorId, 'request_reply', request.marker);
    return response;
  }
}

@Injectable()
class EntryBoundPushHandler implements ZLinkEntrySpotActorRequestHandler<TransferActor, BoundPushReq, BoundPushRes> {
  @ZLinkSpotActorRequest(SpotActorTransferNames.packetBoundPush)
  async handle(actor: TransferActor, _context: ZLinkSpotActorRequestContext, request: BoundPushReq): Promise<BoundPushRes> {
    return await pushBound(actorContextLocation(actor), actor, request);
  }
}

@Injectable()
class BoundPushHandler {
  @ZLinkSpotActorRequest(SpotActorTransferNames.packetBoundPush)
  async handle(actor: TransferActor, _context: ZLinkSpotActorRequestContext, request: BoundPushReq): Promise<BoundPushRes> {
    return await pushBound(actorContextLocation(actor), actor, request);
  }
}

function actorContextLocation(actor: TransferActor): { readonly spotRid: unknown; readonly nodeRid: unknown } {
  return {
    spotRid: actor.context.spotId ?? options.rid,
    nodeRid: options.rid
  };
}

async function pushBound(context: { spotRid: unknown; nodeRid: unknown }, actor: TransferActor, request: BoundPushReq): Promise<BoundPushRes> {
  const response = probeResponse(context, actor, request);
  actor.context.boundSession.send(new BoundPushNotify(
    response.scenario,
    response.actorId,
    response.spotRid,
    response.nodeRid,
    response.stateVersion,
    response.marker
  )).submit();
  evidence.add(request.scenario, actor.actorId, 'bound_push', request.marker);
  return response;
}

function probeResponse(context: { spotRid: unknown; nodeRid: unknown }, actor: TransferActor, request: ProbeReq): ProbeRes {
  return {
    scenario: request.scenario,
    actorId: actor.actorId,
    spotRid: String(context.spotRid),
    nodeRid: String(context.nodeRid),
    stateVersion: actor.stateVersion,
    marker: request.marker
  };
}

@Injectable()
@zlinkRuntimeEventHandler()
class ActorLocationEvidenceRecorder implements ZLinkRuntimeEventHandler<ZLinkLocationActorEvent> {
  async handle(event: ZLinkLocationActorEvent): Promise<void> {
    if (
      event.sourceName !== 'spot-actor-transfer.actor-location'
      || event.event !== ZLinkLocationActorEventKind.RowUpdated
      || event.actor?.spotId === undefined
    ) {
      return;
    }
    const scenario = actorScenarios.get(event.actor.actorId);
    if (scenario === undefined) return;
    evidence.add(
      scenario,
      event.actor.actorId,
      'location_committed',
      `node=${String(event.actor.ownerNodeRid)}|spot=${String(event.actor.spotId)}|generation=${event.actor.actorRef.generation}`
    );
  }
}

interface ActorHandoffRuntimeEvent extends ZLinkRuntimeEvent {
  readonly marker: string;
  readonly actorId: string;
  readonly index?: number;
  readonly requestSeq?: string;
  readonly flags?: number;
}

@Injectable()
@zlinkRuntimeEventHandler()
class ActorHandoffEvidenceRecorder implements ZLinkRuntimeEventHandler<ActorHandoffRuntimeEvent> {
  async handle(event: ActorHandoffRuntimeEvent): Promise<void> {
    if (event.sourceName !== 'zlink.framework.actor-handoff') return;
    const scenario = actorScenarios.get(event.actorId);
    if (scenario === undefined) return;
    const value = event.marker === 'handoff_request_frame'
      ? `index=${event.index ?? ''}|requestSeq=${event.requestSeq ?? ''}|flags=${event.flags ?? ''}`
      : event.index === undefined ? '' : String(event.index);
    evidence.add(scenario, event.actorId, event.marker, value);
  }
}

class ActorNodeModule {}
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
        fs.mkdirSync(options.logDir, { recursive: true });
        evidence = new EvidenceStore(options.rid, options.evidenceFile);
        domainState = new DomainStateStore(options.logDir);
        joinedGates = new GateStore();
        transferGates = new GateStore();
        const builder = zlinkFramework();
        const store = new ZLinkRedisLocationStore({
          url: `redis://${options.redisEndpoint}`,
          keyPrefix: options.redisKeyPrefix
        });
        const relocationStore = new ZLinkRedisRelocationStore({
          url: `redis://${options.redisEndpoint}`,
          keyPrefix: options.redisKeyPrefix
        });
        builder.addLocationStore(store);
        builder.addRelocationStore(relocationStore);
        Object.assign(builder.configureLocations(), {
          pollingIntervalMs: 100,
          heartbeatIntervalMs: 1000,
          ownerLeaseTtlMs: 3000
        });
        builder.configureDispatch()
          .messageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
          .traceLogFile(path.join(options.logDir, `${options.rid}-flow.log`))
          .traceLabel(options.rid);
        builder.setActorTransferForwardWindow(500);
        const mesh = builder.addRouteMesh(SpotActorTransferNames.mesh)
          .listen(options.routerEndpoint).routingId(options.rid);
        const objects = mesh.objects().server();
        objects.addEntrySpot(TransferEntrySpot);
        objects.addActorFactory(
          SpotActorTransferNames.actorTypeStateful,
          TransferActorFactory,
          undefined,
          zlinkRecreateRelocation()
        );
        objects.addActorFactory(
          SpotActorTransferNames.actorTypeEmptyState,
          TransferActorFactory,
          undefined,
          zlinkRecreateRelocation()
        );
        objects.addActorFactory(
          SpotActorTransferNames.actorTypeFailTransferOut,
          TransferActorFactory,
          undefined,
          zlinkRecreateRelocation()
        );
        objects.addActorFactory(
          SpotActorTransferNames.actorTypeFailLeave,
          TransferActorFactory,
          undefined,
          zlinkRecreateRelocation()
        );
        objects.addActorFactory(
          SpotActorTransferNames.actorTypeFailTransferIn,
          TransferActorFactory,
          undefined,
          zlinkRecreateRelocation()
        );
        objects.addActorFactory(
          SpotActorTransferNames.actorTypeNoAdapter,
          NoAdapterActorFactory,
          undefined,
          zlinkRecreateRelocation()
        );
        objects.addSpotFactory(
          TransferUserSpot.name,
          TransferUserSpot,
          undefined,
          zlinkRecreateRelocation()
        );
        mesh.addActorTransferAdapter(
          SpotActorTransferNames.actorTypeStateful,
          TransferActorAdapter
        );
        mesh.channelName(SpotActorTransferNames.mesh);
        return {
          ...builder.build(),
          monitoring: {
            locationActor: [{ sourceName: 'spot-actor-transfer.actor-location' }]
          }
        };
      }
    })
  ],
  providers: [
    TransferActorFactory,
    NoAdapterActorFactory,
    TransferActorAdapter,
    TransferEntrySpot,
    TransferUserSpot,
    JoinTargetHandler,
    UserJoinTargetHandler,
    ProbeHandler,
    HandoffHandler,
    EntryProbeHandler,
    EntryHandoffHandler,
    EntryBoundPushHandler,
    BoundPushHandler,
    ApplyActorLifecycleStateHandler,
    ActorLocationEvidenceRecorder,
    ActorHandoffEvidenceRecorder
  ]
})(ActorNodeModule);

let actorManager: ZLinkActorManager;

async function main(): Promise<void> {
  const app = await NestFactory.createApplicationContext(ActorNodeModule, { logger: false, abortOnError: false });
  actorManager = app.get(ZLINK_ACTOR_MANAGER, { strict: false }) as ZLinkActorManager;
  const actorClient = app.get(ZLINK_ACTOR_CLIENT, { strict: false }) as ZLinkActorClient;
  const spots = app.get(ZLINK_SPOT_MANAGER, { strict: false }) as ZLinkSpotManager;
  const server = await startHttpServer(options.httpUrl, [
    { method: 'GET', path: '/health', handle: () => ({ status: 'ok', rid: options.rid }) },
    { method: 'GET', path: '/evidence', handle: () => evidence.snapshot() },
    {
      method: 'GET', path: /^\/spots\/([^/]+)\/ref$/, handle: async (_body, match) => {
        const spot = await spots.find(match![1]);
        return spot === undefined ? { found: false } : {
          found: true,
          spotRid: String(spot.spotId)
        };
      }
    },
    {
      method: 'POST', path: '/evidence/wait', handle: (body) => {
        const request = body as EvidenceWaitReq;
        return evidence.waitUntil(request.containsAll, Math.max(1, Math.min(request.timeoutMilliseconds ?? 10000, 40000)));
      }
    },
    {
      method: 'POST', path: /^\/joined-gates\/([^/]+)\/release$/, handle: (_body, match) => ({
        key: match![1], released: joinedGates.release(match![1])
      } satisfies GateReleaseRes)
    },
    {
      method: 'POST', path: /^\/transfer-gates\/([^/]+)\/release$/, handle: (_body, match) => ({
        key: match![1], released: transferGates.release(match![1])
      } satisfies GateReleaseRes)
    },
    {
      method: 'POST', path: '/spots', handle: async (body) => {
        const request = body as CreateSpotReq;
        const result = await spots
          .getOrCreate(request.spotRid, TransferUserSpot.name)
          .inMesh(SpotActorTransferNames.mesh)
          .request(request)
          .submit();
        return {
          spotRid: String(result.spot.spotId),
          nodeRid: String(result.spot.nodeRid),
          state: String(result.state)
        } satisfies CreateSpotRes;
      }
    },
    {
      method: 'POST', path: '/actors', handle: async (body) => {
        const request = body as ActorCreateReq;
        const result = await actorManager
          .getOrCreate(request.actorId, request.actorType)
          .inMesh(SpotActorTransferNames.mesh)
          .request(request)
          .submit();
        if (result.status === 'rejected') {
          throw new Error(`Actor '${request.actorId}' creation was rejected.`);
        }
        return { actorId: result.actor.actorId, actorType: request.actorType, nodeRid: String(result.actor.nodeRid), generation: result.actor.generation.toString() } satisfies ActorCreateRes;
      }
    },
    {
      method: 'GET', path: /^\/actors\/([^/]+)\/ref$/, handle: async (_body, match) => {
        const actor = await requireActor(match![1]);
        capturedActorRefs.set(actor.actorId, actor);
        return actorSnapshot(actor);
      }
    },
    {
      method: 'POST', path: /^\/actors\/([^/]+)\/join$/, handle: async (body, match) => {
        const actorId = match![1];
        const input = body as JoinTargetReq;
        const request = new JoinTargetReq(input.scenario, input.targetSpotRid, input.expectedMode, input.transferId);
        try {
          const result = await actorClient.requestToActor(SpotActorTransferNames.mesh, await requireActor(actorId), request)
            .timeout(10000).submit<JoinTargetRes>();
          evidence.add(request.scenario, actorId, result.accepted ? 'success_reply' : 'reject_reply', request.targetSpotRid);
          return result;
        } catch (error) {
          const errorKind = error instanceof Error ? error.message : String(error);
          evidence.add(request.scenario, actorId, 'join_failed', errorKind);
          return { scenario: request.scenario, actorId, accepted: false, sourceNodeRid: options.rid, targetSpotRid: request.targetSpotRid, stateVersion: 0, errorKind } satisfies JoinTargetRes;
        }
      }
    },
    {
      method: 'POST', path: /^\/actors\/([^/]+)\/probe$/, handle: async (body, match) => {
        const input = body as ProbeReq;
        const request = new ProbeReq(input.scenario, input.marker, input.delayMs, input.requestTimeoutMs);
        return await actorClient.requestToActor(SpotActorTransferNames.mesh, await requireActor(match![1]), request)
          .timeout(request.requestTimeoutMs ?? 10000)
          .submit<ProbeRes>();
      }
    },
    {
      method: 'POST', path: /^\/actors\/([^/]+)\/handoff$/, handle: async (body, match) => {
        const input = body as HandoffProbe;
        await actorClient.sendToActor(SpotActorTransferNames.mesh, await requireActor(match![1]), new HandoffProbe(input.scenario, input.marker, input.delayMs, input.requestTimeoutMs)).submit();
        return { accepted: true };
      }
    },
    {
      method: 'POST', path: /^\/actors\/([^/]+)\/handoff-stale$/, handle: async (body, match) => {
        const actor = capturedActorRefs.get(match![1]);
        if (actor === undefined) throw new Error(`Actor '${match![1]}' does not have a captured ref.`);
        const input = body as HandoffProbe;
        await actorClient.sendToActor(SpotActorTransferNames.mesh, actor, new HandoffProbe(input.scenario, input.marker, input.delayMs, input.requestTimeoutMs)).submit();
        return { accepted: true };
      }
    },
    {
      method: 'POST', path: /^\/actors\/([^/]+)\/probe-stale$/, handle: async (body, match) => {
        const actor = capturedActorRefs.get(match![1]);
        if (actor === undefined) throw new Error(`Actor '${match![1]}' does not have a captured ref.`);
        const input = body as ProbeReq;
        return await actorClient.requestToActor(SpotActorTransferNames.mesh, actor, new ProbeReq(input.scenario, input.marker, input.delayMs, input.requestTimeoutMs))
          .timeout(10000).submit<ProbeRes>();
      }
    },
    {
      method: 'POST', path: /^\/actors\/([^/]+)\/bound-push$/, handle: async (body, match) => {
        const input = body as BoundPushReq;
        return actorClient.requestToActor(SpotActorTransferNames.mesh, await requireActor(match![1]), new BoundPushReq(input.scenario, input.marker))
          .timeout(10000).submit<BoundPushRes>();
      }
    },
    {
      method: 'POST', path: '/crash', handle: () => {
        setTimeout(() => process.exit(86), 25);
        return { status: 'crashing' };
      }
    },
    { method: 'POST', path: '/shutdown', handle: () => { stopping = true; return { status: 'stopping' }; } }
  ]);
  while (!stopping) await new Promise((resolve) => setTimeout(resolve, 100));
  await closeHttpServer(server);
  await app.close();
}

async function requireActor(actorId: string): Promise<ActorRef> {
  const actor = await actorManager.find(actorId);
  if (actor === undefined) throw new Error(`Actor '${actorId}' was not found.`);
  return actor;
}

function actorSnapshot(actor: ActorRef): ActorRefSnapshotRes {
  return { actorId: actor.actorId, nodeRid: String(actor.nodeRid), generation: actor.generation.toString() };
}

async function delay(milliseconds: number): Promise<void> {
  await new Promise((resolve) => setTimeout(resolve, milliseconds));
}

class GateStore {
  private readonly gates = new Map<string, { promise: Promise<void>; resolve: () => void; released: boolean }>();

  wait(key: string, signal?: AbortSignal): Promise<void> {
    let gate = this.gates.get(key);
    if (gate === undefined) {
      let resolve!: () => void;
      const promise = new Promise<void>((done) => { resolve = done; });
      gate = { promise, resolve, released: false };
      this.gates.set(key, gate);
    }
    return signal === undefined ? gate.promise : Promise.race([
      gate.promise,
      new Promise<void>((_resolve, reject) => signal.addEventListener('abort', () => reject(signal.reason), { once: true }))
    ]);
  }

  release(key: string): boolean {
    let gate = this.gates.get(key);
    if (gate === undefined) {
      let resolve!: () => void;
      const promise = new Promise<void>((done) => { resolve = done; });
      gate = { promise, resolve, released: false };
      this.gates.set(key, gate);
    }
    if (gate.released) return false;
    gate.released = true;
    gate.resolve();
    return true;
  }
}

class DomainStateStore {
  constructor(private readonly directory: string) {}
  save(actorId: string, stateVersion: number): void {
    fs.writeFileSync(path.join(this.directory, `domain-${actorId}.state`), String(stateVersion));
  }
  load(actorId: string): number {
    return Number(fs.readFileSync(path.join(this.directory, `domain-${actorId}.state`), 'utf8'));
  }
}

main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});

export {};
