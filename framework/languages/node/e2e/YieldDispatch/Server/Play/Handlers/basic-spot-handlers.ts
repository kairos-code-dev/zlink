import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotPacketHandler, ZLinkSpotRequestHandler } from '@zlink-systems/framework';
import type {
  DelayRes,
  DelayReq,
  HoldMsg,
  ProbeMsg,
  WorkerYieldMsg,
  YieldMsg,
  YieldReq,
  YieldDispatchRes
} from '../../../Shared/messages';
import { YieldDispatchNames } from '../../../Shared/messages';
import { EvidenceStore } from '../../Support/evidence-store';
import type { YieldProbeSpot } from '../Spots/yield-probe-spot';

@Injectable()
export class HoldCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, HoldMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: YieldProbeSpot, request: HoldMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(`hold-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}|handler=spot`);
    await spot.context.outbound
      .requestToChannel(YieldDispatchNames.delayChannel, {
        requestId: request.requestId,
        delayMs: request.delayMs,
        marker: 'hold'
      } satisfies DelayReq)
      .packetName('DelayReq')
      .timeout(5000)
      .submit<DelayRes>();
    this.evidence.add(`hold-resumed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}|handler=spot`);
    this.evidence.add(`hold-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}|handler=spot`);
  }
}

@Injectable()
export class YieldCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, YieldMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: YieldProbeSpot, request: YieldMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(
      `yield-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}`
      + `|correlation=${request.correlationId}|handler=spot`
    );
    const call = spot.context.outbound
      .requestToChannel(YieldDispatchNames.delayChannel, {
        requestId: request.requestId,
        delayMs: request.delayMs,
        marker: 'yield'
      } satisfies DelayReq)
      .packetName('DelayReq')
      .timeout(5000);
    this.evidence.add(
      `yield-released|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}`
      + `|correlation=${request.correlationId}|handler=spot`
    );
    await call.yield<DelayRes>();
    this.evidence.add(
      `yield-resumed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}`
      + `|correlation=${request.correlationId}|handler=spot`
    );
    this.evidence.add(
      `yield-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}`
      + `|correlation=${request.correlationId}|handler=spot`
    );
  }
}

@Injectable()
export class YieldRequestHandler implements ZLinkSpotRequestHandler<YieldProbeSpot, YieldReq, YieldDispatchRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    request: YieldReq,
    context: ZLinkHandlerContext
  ): Promise<YieldDispatchRes> {
    await new YieldCommandHandler(this.evidence).handle(spot, request, context);
    return {
      scenarioId: request.correlationId === 'remote-spot' ? 'YD-D2-target' : 'YD-A2',
      requestId: request.requestId,
      spotRid: String(spot.context.spotRid),
      nodeRid: String(spot.context.nodeRid),
      marker: 'yield-completed'
    };
  }
}

@Injectable()
export class WorkerYieldCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, WorkerYieldMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: YieldProbeSpot, request: WorkerYieldMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(`worker-yield-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}|handler=spot`);
    const call = spot.context.runWorker(async (signal) => {
      await delay(request.delayMs, signal);
      return request.requestId;
    });
    this.evidence.add(`worker-yield-released|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}|handler=spot`);
    await call.yield();
    this.evidence.add(`worker-yield-resumed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}|handler=spot`);
    this.evidence.add(`worker-yield-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}|handler=spot`);
  }
}

@Injectable()
export class ProbeCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, ProbeMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: YieldProbeSpot, request: ProbeMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(
      `probe-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}`
      + `|marker=${request.marker}|handler=spot`
    );
    this.evidence.add(
      `probe-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}|request=${request.requestId}`
      + `|marker=${request.marker}|handler=spot`
    );
  }
}

function delay(delayMs: number, signal: AbortSignal): Promise<void> {
  return new Promise<void>((resolve, reject) => {
    if (signal.aborted) {
      reject(signal.reason instanceof Error ? signal.reason : new Error('Worker yield was aborted.'));
      return;
    }
    const timer = setTimeout(resolve, delayMs);
    signal.addEventListener('abort', () => {
      clearTimeout(timer);
      reject(signal.reason instanceof Error ? signal.reason : new Error('Worker yield was aborted.'));
    }, { once: true });
  });
}
