import { Inject, Injectable } from '@nestjs/common';
import type {
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorFactory,
  ZLinkActorJoinRequest,
  ZLinkActorMembership,
  ZLinkEntrySpot,
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
  ZLinkSpotManager,
  ZLinkSpotRequestHandler
} from '@zlink-systems/framework';
import { ZLINK_ROUTE_CLIENT, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import { ZLinkPacket, ZLinkSpotActorRequest } from '@zlink-systems/framework';
import type {
  MultiNodeCreateSpotRes,
  MultiNodeCreateSpotReq,
  ScaleOutActorProbeReq,
  ScaleOutActorProbeRes,
  SpotOnlyJoinReq,
  SpotOnlyJoinRes,
  SpotOnlyMeshReq,
  SpotOnlyMeshRes,
  StateRes
} from '../../../Shared/messages';
import { SpotServiceNames, StateMsg, StateReq, spotServicePacket } from '../../../Shared/messages';
import { EvidenceStore } from '../Infrastructure/evidence-store';

export class MultiNodeSpotA implements ZLinkSpot {
  private static evidence?: EvidenceStore;
  private value = 0;
  readonly context!: ZLinkSpotContext;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  configure(): void {
    this.context.handlers.addPacket(MultiNodeStateAHandler);
  }

  async onInitialize(): Promise<void> {
    MultiNodeSpotA.requireEvidence()
      .add(`multi-spot-initialize|node=${SpotServiceNames.multiSpotNodeA}|spot=${this.context.spotRid}`);
  }

  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }
  async onJoinedActor(): Promise<void> {}
  async onLeaveActor(): Promise<void> {}
  async onDisconnectActor(): Promise<void> {}

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
    this.context.handlers.addPacket(MultiNodeStateBHandler);
  }

  async onInitialize(): Promise<void> {
    MultiNodeSpotB.requireEvidence()
      .add(`multi-spot-initialize|node=${SpotServiceNames.multiSpotNodeB}|spot=${this.context.spotRid}`);
  }

  async onActorJoin(): Promise<{ accepted: boolean }> { return { accepted: true }; }
  async onJoinedActor(): Promise<void> {}
  async onLeaveActor(): Promise<void> {}
  async onDisconnectActor(): Promise<void> {}

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

  configure(): void {
    this.context.handlers.addHandler(MultiNodeSpotOnlyJoinHandler);
    this.context.handlers.addHandler(ScaleOutActorProbeHandler);
  }
}

export class MultiNodeScenarioActorFactory implements ZLinkActorFactory {
  async create(actorId: string, context: ZLinkActorContext): Promise<ZLinkActor> {
    return new MultiNodeScenarioActor(actorId, context);
  }
}

export class MultiNodeEntrySpot implements ZLinkEntrySpot<MultiNodeScenarioActor> {
  private static evidence?: EvidenceStore;
  readonly context!: ZLinkEntrySpotContext<MultiNodeScenarioActor>;

  static useEvidence(evidence: EvidenceStore): void {
    this.evidence = evidence;
  }

  async onCreateActor(actor: ZLinkActorMembership): Promise<void> {
    MultiNodeEntrySpot.requireEvidence()
      .add(`entry-created|rid=${MultiNodeEntrySpot.requireEvidence().rid}|actor=${actor.actor.actorId}`);
  }

  async onActorJoin(actor: ZLinkActorJoinRequest): Promise<{ accepted: boolean }> {
    void actor;
    return { accepted: true };
  }

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    MultiNodeEntrySpot.requireEvidence()
      .add(`entry-joined|rid=${MultiNodeEntrySpot.requireEvidence().rid}|actor=${actor.actor.actorId}`);
  }

  async onLeaveActor(): Promise<void> {}
  async onDisconnectActor(): Promise<void> {}

  static requireEvidence(): EvidenceStore {
    if (this.evidence === undefined) {
      throw new Error('MultiNodeEntrySpot evidence store is not configured.');
    }
    return this.evidence;
  }
}

export class SpotOnlyUserSpot implements ZLinkSpot<MultiNodeScenarioActor> {
  private static evidence?: EvidenceStore;
  private static refs?: ZLinkSpotManager;
  private value = 0;
  readonly context!: ZLinkSpotContext<MultiNodeScenarioActor>;

  static configureDependencies(evidence: EvidenceStore, refs: ZLinkSpotManager): void {
    this.evidence = evidence;
    this.refs = refs;
  }

  configure(): void {
    this.context.handlers.addPacket(SpotOnlyStateReqHandler);
    this.context.handlers.addPacket(SpotOnlyStateMsgHandler);
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
    const target = await SpotOnlyUserSpot.requireRefs().find(request.targetSpotRid);
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

  async onActorJoin(actor: ZLinkActorJoinRequest): Promise<{ accepted: boolean }> {
    SpotOnlyUserSpot.requireEvidence()
      .add(`spot-actor-admitted|rid=${SpotOnlyUserSpot.requireEvidence().rid}|spot=${this.context.spotRid}|actor=${actor.actor.actorId}`);
    return { accepted: true };
  }

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> {
    SpotOnlyUserSpot.requireEvidence()
      .add(`spot-actor-joined|rid=${SpotOnlyUserSpot.requireEvidence().rid}|spot=${this.context.spotRid}|actor=${actor.actor.actorId}`);
  }

  async onLeaveActor(): Promise<void> {}
  async onDisconnectActor(): Promise<void> {}

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

  static requireRefs(): ZLinkSpotManager {
    if (this.refs === undefined) {
      throw new Error('SpotOnlyUserSpot refs are not configured.');
    }
    return this.refs;
  }
}

@Injectable()
export class ScaleOutActorProbeHandler
{
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ScaleOutActorProbeReq')
  async handle(
    actor: MultiNodeScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: ScaleOutActorProbeReq
  ): Promise<ScaleOutActorProbeRes> {
    void context;
    this.evidence.add(
      `scale-out-actor-probe|rid=${this.evidence.rid}|spot=${actor.context.spotRid ?? this.evidence.rid}`
      + `|actor=${actor.actorId}|marker=${request.marker}`
    );
    return { actorId: actor.actorId, nodeRid: this.evidence.rid, marker: request.marker };
  }
}

async function requestSpotOnlyState(
  outbound: ZLinkSpotOutbound,
  target: Awaited<ReturnType<ZLinkSpotManager['find']>>,
  request: StateReq
): Promise<StateRes> {
  if (target === undefined) {
    throw new Error('Spot-only target is missing.');
  }
  return await outbound
    .requestToSpot(target, spotServicePacket(StateReq, request))
    .timeout(2000)
    .submit<StateRes>();
}

async function sendSpotOnlyState(
  outbound: ZLinkSpotOutbound,
  target: Awaited<ReturnType<ZLinkSpotManager['find']>>,
  message: StateMsg
): Promise<void> {
  if (target === undefined) {
    throw new Error('Spot-only target is missing.');
  }
  await outbound
    .sendToSpot(target, spotServicePacket(StateMsg, message))
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
    const result = await this.spots.getOrCreate(
      SpotServiceNames.multiSpotNodeA,
      MultiNodeSpotA,
      request.spotRid
    );
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
    const result = await this.spots.getOrCreate(
      SpotServiceNames.multiSpotNodeB,
      MultiNodeSpotB,
      request.spotRid
    );
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
@ZLinkPacket('StateReq')
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
@ZLinkPacket('StateReq')
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
@ZLinkPacket('StateReq')
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
@ZLinkPacket('StateMsg')
export class SpotOnlyStateMsgHandler implements ZLinkSpotPacketHandler<SpotOnlyUserSpot, StateMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: SpotOnlyUserSpot, message: StateMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(`spot-state-command|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|marker=${message.marker}`);
  }
}

@Injectable()
export class MultiNodeSpotOnlyJoinHandler
{
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('SpotOnlyJoinReq')
  async handle(
    actor: MultiNodeScenarioActor,
    context: ZLinkSpotActorRequestContext,
    request: SpotOnlyJoinReq
  ): Promise<SpotOnlyJoinRes> {
    void context;
    const joined = await actor.context
      .joinSpot(request.targetSpotRid, {})
      .timeout(10000)
      .submit();
    this.evidence.add(
      `spot-only-actor-join|rid=${this.evidence.rid}|actor=${actor.actorId}`
      + `|target=${request.targetSpotRid}|accepted=${joined.status === 'accepted'}|marker=${request.marker}`
    );
    return {
      targetSpotRid: request.targetSpotRid,
      actorId: actor.actorId,
      accepted: joined.status === 'accepted',
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
    ? await spots.getOrCreate(nodeRid, MultiNodeSpotA, spotRid)
    : await spots.getOrCreate(nodeRid, MultiNodeSpotB, spotRid);
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
    .requestToNode(channelName, spotRid, spotServicePacket(StateReq, { operation: 'add', delta }))
    .timeout(2000)
    .submit<StateRes>();
}

export async function requestStateViaSpotOutbound(
  outbound: ZLinkSpotOutbound,
  spotRefs: ZLinkSpotManager,
  _meshName: string,
  spotRid: string,
  delta: number
): Promise<StateRes> {
  const spot = await spotRefs.find(spotRid);
  if (spot === undefined) {
    throw new Error(`SpotRef '${spotRid}' was not found.`);
  }
  return await outbound
    .requestToSpot(spot, spotServicePacket(StateReq, { operation: 'add', delta }))
    .timeout(2000)
    .submit<StateRes>();
}
