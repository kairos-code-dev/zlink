import { Inject, Injectable } from '@nestjs/common';
import type {
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorFactory,
  ZLinkEntrySpot,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkEntrySpotContext,
  ZLinkHandlerContext,
  ZLinkMessage,
  ZLinkRouteClient,
  ZLinkRouteRequestHandler,
  ZLinkSpot,
  ZLinkSpotActorRequestContext,
  ZLinkSpotContext,
  ZLinkSpotManager,
  ZLinkSpotOutbound,
  ZLinkSpotPacketHandler,
  ZLinkSpotRefResolver,
  ZLinkSpotRequestHandler
} from '@zlink-systems/framework';
import { ZLINK_ROUTE_CLIENT, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import type {
  MultiNodeCreateSpotRes,
  MultiNodeCreateSpotReq,
  SpotOnlyJoinReq,
  SpotOnlyJoinRes,
  SpotOnlyMeshReq,
  SpotOnlyMeshRes,
  StateMsg,
  StateRes,
  StateReq
} from '../../../Shared/messages';
import { SpotServiceNames } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

export class MultiNodeSpotA implements ZLinkSpot {
  private static evidence?: EvidenceStore;
  private value = 0;
  readonly context!: ZLinkSpotContext;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  configure(): void {
    this.context.handlers.packet('StateReq', MultiNodeStateAHandler);
  }

  async onInitialize(): Promise<void> {
    MultiNodeSpotA.requireEvidence()
      .add(`multi-spot-initialize|node=${SpotServiceNames.multiSpotNodeA}|spot=${this.context.spotRid}`);
  }

  add(delta: number): number {
    this.value += delta;
    return this.value;
  }

  static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('MultiNodeSpotA evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class MultiNodeSpotB implements ZLinkSpot {
  private static evidence?: EvidenceStore;
  private value = 0;
  readonly context!: ZLinkSpotContext;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  configure(): void {
    this.context.handlers.packet('StateReq', MultiNodeStateBHandler);
  }

  async onInitialize(): Promise<void> {
    MultiNodeSpotB.requireEvidence()
      .add(`multi-spot-initialize|node=${SpotServiceNames.multiSpotNodeB}|spot=${this.context.spotRid}`);
  }

  add(delta: number): number {
    this.value += delta;
    return this.value;
  }

  static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('MultiNodeSpotB evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class MultiNodeScenarioActor implements ZLinkActor {
  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {}
}

export class MultiNodeScenarioActorFactory implements ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext): ZLinkActor {
    return new MultiNodeScenarioActor(actorId, context);
  }
}

export class MultiNodeEntrySpot implements ZLinkEntrySpot<MultiNodeScenarioActor> {
  private static evidence?: EvidenceStore;
  readonly context!: ZLinkEntrySpotContext<MultiNodeScenarioActor>;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  configure(): void {
    this.context.handlers.actorRequest('SpotOnlyJoinReq', MultiNodeSpotOnlyJoinHandler, MultiNodeScenarioActor);
  }

  async onCreateActor(actor: MultiNodeScenarioActor): Promise<void> {
    MultiNodeEntrySpot.requireEvidence()
      .add(`entry-created|rid=${MultiNodeEntrySpot.requireEvidence().rid}|actor=${actor.actorId}`);
  }

  async onActorJoin(actor: MultiNodeScenarioActor): Promise<{ accepted: boolean }> {
    void actor;
    return { accepted: true };
  }

  async onJoinedActor(actor: MultiNodeScenarioActor): Promise<void> {
    MultiNodeEntrySpot.requireEvidence()
      .add(`entry-joined|rid=${MultiNodeEntrySpot.requireEvidence().rid}|actor=${actor.actorId}`);
  }

  static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('MultiNodeEntrySpot evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class SpotOnlyUserSpot implements ZLinkSpot<MultiNodeScenarioActor> {
  private static evidence?: EvidenceStore;
  private static refs?: ZLinkSpotRefResolver;
  private value = 0;
  readonly context!: ZLinkSpotContext<MultiNodeScenarioActor>;

  static configureDependencies(evidence: EvidenceStore, refs: ZLinkSpotRefResolver): void {
    this.evidence = evidence;
    this.refs = refs;
  }

  configure(): void {
    this.context.handlers.packet('StateReq', SpotOnlyStateReqHandler);
    this.context.handlers.packet('StateMsg', SpotOnlyStateMsgHandler);
  }

  async onInitialize(): Promise<void> {
    SpotOnlyUserSpot.requireEvidence()
      .add(`spot-initialize|rid=${SpotOnlyUserSpot.requireEvidence().rid}|spot=${this.context.spotRid}`);
  }

  async onCreate(request: ZLinkMessage): Promise<{ accepted: boolean }> {
    SpotOnlyUserSpot.requireEvidence()
      .add(`spot-created|rid=${SpotOnlyUserSpot.requireEvidence().rid}|spot=${this.context.spotRid}`);
    const command = request.decode<SpotOnlyMeshReq | undefined>(Object as never);
    if (command !== undefined) {
      await this.requestSend(command);
    }
    return { accepted: true };
  }

  async requestSend(request: SpotOnlyMeshReq): Promise<StateRes> {
    const target = await SpotOnlyUserSpot.requireRefs().resolveSpotRef(request.targetSpotRid);
    const reply = await requestSpotOnlyState(
      this.context.outbound,
      target,
      { operation: 'add', delta: 7 } satisfies StateReq
    );
    await sendSpotOnlyState(
      this.context.outbound,
      target,
      { marker: `sm-f6-send-${request.marker}` } satisfies StateMsg
    );
    SpotOnlyUserSpot.requireEvidence().add(
      `spot-only-request|rid=${SpotOnlyUserSpot.requireEvidence().rid}|source=${this.context.spotRid}`
      + `|target=${request.targetSpotRid}|value=${reply.value}|marker=${request.marker}`
    );
    return reply;
  }

  async onActorJoin(actor: MultiNodeScenarioActor): Promise<{ accepted: boolean }> {
    SpotOnlyUserSpot.requireEvidence()
      .add(`spot-actor-joined|rid=${SpotOnlyUserSpot.requireEvidence().rid}|spot=${this.context.spotRid}|actor=${actor.actorId}`);
    return { accepted: true };
  }

  add(delta: number): number {
    this.value += delta;
    return this.value;
  }

  static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('SpotOnlyUserSpot evidence store is not configured.');
    }
    return this.evidence;
  }

  static requireRefs(): ZLinkSpotRefResolver {
    if (this.refs === undefined) {
      throw new Error('SpotOnlyUserSpot refs are not configured.');
    }
    return this.refs;
  }
}

async function requestSpotOnlyState(
  outbound: ZLinkSpotOutbound,
  target: Awaited<ReturnType<ZLinkSpotRefResolver['resolveSpotRef']>>,
  request: StateReq
): Promise<StateRes> {
  if (target === undefined) {
    throw new Error('Spot-only target is missing.');
  }
  return await outbound
    .requestToSpot(target, request)
    .packetName('StateReq')
    .timeout(2000)
    .submit<StateRes>();
}

async function sendSpotOnlyState(
  outbound: ZLinkSpotOutbound,
  target: Awaited<ReturnType<ZLinkSpotRefResolver['resolveSpotRef']>>,
  message: StateMsg
): Promise<void> {
  if (target === undefined) {
    throw new Error('Spot-only target is missing.');
  }
  await outbound
    .sendToSpot(target, message)
    .packetName('StateMsg')
    .submit();
}

@Injectable()
export class MultiNodeCreateSpotAHandler implements ZLinkRouteRequestHandler<MultiNodeCreateSpotReq, MultiNodeCreateSpotRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER)
    private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_ROUTE_CLIENT)
    private readonly routes: ZLinkRouteClient,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: MultiNodeCreateSpotReq): Promise<MultiNodeCreateSpotRes> {
    const result = await this.spots.getOrCreate(MultiNodeSpotA, request.spotRid);
    const state = await requestState(this.routes, SpotServiceNames.multiRouteChannelA, request.spotRid, request.delta);
    this.evidence.add(`multi-create-spot|node=${SpotServiceNames.multiSpotNodeA}|spot=${result.spotRid}|state=${result.state}`);
    return {
      spotRid: String(result.spotRid),
      nodeRid: SpotServiceNames.multiSpotNodeA,
      state: String(result.state),
      value: state.value
    };
  }
}

@Injectable()
export class MultiNodeCreateSpotBHandler implements ZLinkRouteRequestHandler<MultiNodeCreateSpotReq, MultiNodeCreateSpotRes> {
  constructor(
    @Inject(ZLINK_SPOT_MANAGER)
    private readonly spots: ZLinkSpotManager,
    @Inject(ZLINK_ROUTE_CLIENT)
    private readonly routes: ZLinkRouteClient,
    private readonly evidence: EvidenceStore
  ) {}

  async handle(request: MultiNodeCreateSpotReq): Promise<MultiNodeCreateSpotRes> {
    const result = await this.spots.getOrCreate(MultiNodeSpotB, request.spotRid);
    const state = await requestState(this.routes, SpotServiceNames.multiRouteChannelB, request.spotRid, request.delta);
    this.evidence.add(`multi-create-spot|node=${SpotServiceNames.multiSpotNodeB}|spot=${result.spotRid}|state=${result.state}`);
    return {
      spotRid: String(result.spotRid),
      nodeRid: SpotServiceNames.multiSpotNodeB,
      state: String(result.state),
      value: state.value
    };
  }
}

@Injectable()
export class MultiNodeStateAHandler implements ZLinkSpotRequestHandler<MultiNodeSpotA, StateReq, StateRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: MultiNodeSpotA, request: StateReq, context: ZLinkHandlerContext): Promise<StateRes> {
    void context;
    const value = spot.add(request.operation === 'add' ? request.delta : 0);
    this.evidence.add(`multi-state-request|node=${SpotServiceNames.multiSpotNodeA}|spot=${spot.context.spotRid}|value=${value}`);
    return {
      spotRid: String(spot.context.spotRid),
      nodeRid: String(spot.context.nodeRid),
      value
    };
  }
}

@Injectable()
export class MultiNodeStateBHandler implements ZLinkSpotRequestHandler<MultiNodeSpotB, StateReq, StateRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: MultiNodeSpotB, request: StateReq, context: ZLinkHandlerContext): Promise<StateRes> {
    void context;
    const value = spot.add(request.operation === 'add' ? request.delta : 0);
    this.evidence.add(`multi-state-request|node=${SpotServiceNames.multiSpotNodeB}|spot=${spot.context.spotRid}|value=${value}`);
    return {
      spotRid: String(spot.context.spotRid),
      nodeRid: String(spot.context.nodeRid),
      value
    };
  }
}

@Injectable()
export class SpotOnlyStateReqHandler implements ZLinkSpotRequestHandler<SpotOnlyUserSpot, StateReq, StateRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: SpotOnlyUserSpot, request: StateReq, context: ZLinkHandlerContext): Promise<StateRes> {
    void context;
    const value = spot.add(request.operation === 'add' ? request.delta : 0);
    this.evidence.add(`spot-state-request|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|value=${value}`);
    return {
      spotRid: String(spot.context.spotRid),
      nodeRid: String(spot.context.nodeRid),
      value
    };
  }
}

@Injectable()
export class SpotOnlyStateMsgHandler implements ZLinkSpotPacketHandler<SpotOnlyUserSpot, StateMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: SpotOnlyUserSpot, message: StateMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(`spot-state-command|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|marker=${message.marker}`);
  }
}

@Injectable()
export class MultiNodeSpotOnlyJoinHandler
  implements ZLinkEntrySpotActorRequestHandler<MultiNodeEntrySpot, MultiNodeScenarioActor, SpotOnlyJoinReq, SpotOnlyJoinRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    entrySpot: MultiNodeEntrySpot,
    actor: MultiNodeScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: SpotOnlyJoinReq
  ): Promise<SpotOnlyJoinRes> {
    void entrySpot;
    void context;
    const joined = await actor.context
      .joinSpot(request.targetSpotRid, {})
      .timeout(10000)
      .submit();
    this.evidence.add(
      `spot-only-actor-join|rid=${this.evidence.rid}|actor=${actor.actorId}`
      + `|target=${request.targetSpotRid}|accepted=${joined.accepted}|marker=${request.marker}`
    );
    return {
      targetSpotRid: request.targetSpotRid,
      actorId: actor.actorId,
      accepted: joined.accepted,
      marker: request.marker
    };
  }
}

export async function createLocalMultiNodeSpot(
  spots: ZLinkSpotManager,
  evidence: EvidenceStore,
  nodeRid: string,
  spotRid: string
): Promise<MultiNodeCreateSpotRes> {
  const created = nodeRid === SpotServiceNames.multiSpotNodeA
    ? await spots.getOrCreate(MultiNodeSpotA, spotRid)
    : await spots.getOrCreate(MultiNodeSpotB, spotRid);
  evidence.add(`multi-create-spot|node=${nodeRid}|spot=${created.spotRid}|state=${created.state}`);
  return {
    spotRid: String(created.spotRid),
    nodeRid,
    state: String(created.state),
    value: 0
  };
}

export async function requestState(
  routes: ZLinkRouteClient,
  channelName: string,
  spotRid: string,
  delta: number
): Promise<StateRes> {
  return await routes
    .requestToNode(channelName, spotRid, { operation: 'add', delta } satisfies StateReq)
    .packetName('StateReq')
    .timeout(2000)
    .submit<StateRes>();
}

export async function requestStateViaSpotOutbound(
  outbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotRefResolver,
  spotRid: string,
  delta: number
): Promise<StateRes> {
  const spot = await spotRefs.resolveSpotRef(spotRid);
  if (spot === undefined) {
    throw new Error(`SpotRef '${spotRid}' was not found.`);
  }
  return await outbound
    .requestToSpot(spot, { operation: 'add', delta } satisfies StateReq)
    .packetName('StateReq')
    .timeout(2000)
    .submit<StateRes>();
}
