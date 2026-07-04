import { Inject, Injectable } from '@nestjs/common';
import type {
  ZLinkHandlerContext,
  ZLinkRouteClient,
  ZLinkRouteRequestHandler,
  ZLinkSpot,
  ZLinkSpotContext,
  ZLinkSpotManager,
  ZLinkSpotOutbound,
  ZLinkSpotRequestHandler
} from '@zlink-systems/framework';
import { ZLINK_ROUTE_CLIENT, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import type {
  MultiNodeCreateSpotRes,
  MultiNodeCreateSpotReq,
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
    const state = await requestStateWithRetry(this.routes, SpotServiceNames.multiRouteChannelA, request.spotRid, request.delta);
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
    const state = await requestStateWithRetry(this.routes, SpotServiceNames.multiRouteChannelB, request.spotRid, request.delta);
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

export async function requestStateWithRetry(
  routes: ZLinkRouteClient,
  channelName: string,
  spotRid: string,
  delta: number
): Promise<StateRes> {
  const deadline = Date.now() + 10000;
  let lastError: unknown;
  while (Date.now() < deadline) {
    try {
      return await routes
        .requestToNode(channelName, spotRid, { operation: 'add', delta } satisfies StateReq)
        .packetName('StateReq')
        .timeout(2000)
        .submit<StateRes>();
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  throw new Error(
    lastError instanceof Error
      ? `Timed out waiting for multi-node spot route '${spotRid}' on '${channelName}'. Last error: ${lastError.message}`
      : `Timed out waiting for multi-node spot route '${spotRid}' on '${channelName}'.`
  );
}

export async function requestStateViaSpotOutboundWithRetry(
  outbound: ZLinkSpotOutbound,
  spotRid: string,
  delta: number
): Promise<StateRes> {
  const deadline = Date.now() + 10000;
  let lastError: unknown;
  while (Date.now() < deadline) {
    try {
      return await outbound
        .requestToSpot(spotRid, { operation: 'add', delta } satisfies StateReq)
        .packetName('StateReq')
        .timeout(2000)
        .submit<StateRes>();
    } catch (error) {
      lastError = error;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
  }
  throw new Error(
    lastError instanceof Error
      ? `Timed out waiting for multi-node spot route '${spotRid}'. Last error: ${lastError.message}`
      : `Timed out waiting for multi-node spot route '${spotRid}'.`
  );
}
