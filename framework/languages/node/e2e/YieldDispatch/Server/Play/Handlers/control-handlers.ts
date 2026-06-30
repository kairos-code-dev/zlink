import { Inject, Injectable } from '@nestjs/common';
import type { ZLinkActorManager, ZLinkHandlerContext, ZLinkRequestHandler, ZLinkSpotManager } from '@zlink-systems/framework';
import { ZLINK_ACTOR_MANAGER, ZLINK_SPOT_MANAGER } from '@zlink-systems/nestjs';
import type {
  BindYieldActorsReply,
  BindYieldActorsReq,
  EnsureSpotReply,
  EnsureSpotReq,
  YieldEvidenceReply,
  YieldEvidenceReq,
  YieldEvidenceWaitReq
} from '../../../Shared/messages';
import { YieldDispatchNames } from '../../../Shared/messages';
import { EvidenceStore } from '../../Support/evidence-store';
import { YieldProbeSpot } from '../Spots/yield-probe-spot';

@Injectable()
export class EnsureSpotControlHandler implements ZLinkRequestHandler<EnsureSpotReq, EnsureSpotReply> {
  constructor(@Inject(ZLINK_SPOT_MANAGER) private readonly spots: ZLinkSpotManager) {}

  async handle(request: EnsureSpotReq, context: ZLinkHandlerContext): Promise<EnsureSpotReply> {
    void context;
    const created = await this.spots.getOrCreate(YieldProbeSpot, request.spotRid);
    return {
      spotRid: String(created.spotRid),
      nodeRid: 'play-a'
    };
  }
}

@Injectable()
export class BindYieldActorsControlHandler implements ZLinkRequestHandler<BindYieldActorsReq, BindYieldActorsReply> {
  constructor(@Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager) {}

  async handle(request: BindYieldActorsReq, context: ZLinkHandlerContext): Promise<BindYieldActorsReply> {
    void context;
    const actors = await Promise.all(request.actorIds.map(async (actorId) => {
      const actor = await this.actors.getOrCreate(actorId, YieldDispatchNames.actorType, { spotRid: request.spotRid });
      return {
        actorId: actor.actorId,
        nodeRid: String(actor.nodeRid),
        generation: actor.generation.toString()
      };
    }));
    return {
      spotRid: request.spotRid,
      actors
    };
  }
}

@Injectable()
export class YieldEvidenceControlHandler implements ZLinkRequestHandler<YieldEvidenceReq, YieldEvidenceReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: YieldEvidenceReq, context: ZLinkHandlerContext): Promise<YieldEvidenceReply> {
    void context;
    return {
      requestId: request.requestId,
      evidence: this.evidence.snapshot()
    };
  }
}

@Injectable()
export class YieldEvidenceWaitControlHandler implements ZLinkRequestHandler<YieldEvidenceWaitReq, YieldEvidenceReply> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(request: YieldEvidenceWaitReq, context: ZLinkHandlerContext): Promise<YieldEvidenceReply> {
    void context;
    const timeoutMs = Math.max(1, Math.min(request.timeoutMilliseconds ?? 20000, 30000));
    const snapshot = await this.evidence.waitUntil((entries) =>
      entries.some((entry) => entry.includes(`request=${request.requestId}`) && entry.includes(request.marker)), timeoutMs);
    return {
      requestId: request.requestId,
      evidence: snapshot
    };
  }
}
