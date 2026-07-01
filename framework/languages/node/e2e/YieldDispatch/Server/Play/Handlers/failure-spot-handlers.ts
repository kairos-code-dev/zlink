import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotPacketHandler } from '@zlink-systems/framework';
import type { DelayRes, DelayReq, YieldCancelMsg, YieldTimeoutMsg } from '../../../Shared/messages';
import { YieldDispatchNames } from '../../../Shared/messages';
import { EvidenceStore } from '../Support/evidence-store';
import type { YieldProbeSpot } from '../Spots/yield-probe-spot';

@Injectable()
export class YieldTimeoutCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, YieldTimeoutMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: YieldProbeSpot, request: YieldTimeoutMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(
      `timeout-yield-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
      + `|request=${request.requestId}|handler=spot`
    );
    try {
      const call = spot.context.outbound
        .requestToChannel(YieldDispatchNames.delayChannel, {
          requestId: request.requestId,
          delayMs: request.delayMs,
          marker: 'timeout'
        } satisfies DelayReq)
        .packetName('DelayReq')
        .timeout(request.timeoutMs);
      this.evidence.add(
        `timeout-yield-released|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${request.requestId}|handler=spot`
      );
      await call.yield<DelayRes>();
      this.evidence.add(
        `timeout-yield-unexpected-resumed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${request.requestId}|handler=spot`
      );
    } catch (error) {
      const errorName = error instanceof Error ? error.name : 'Error';
      this.evidence.add(
        `timeout-yield-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${request.requestId}|error=${errorName}|handler=spot`
      );
    }
  }
}

@Injectable()
export class YieldCancelCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, YieldCancelMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: YieldProbeSpot, request: YieldCancelMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    this.evidence.add(
      `cancel-yield-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
      + `|request=${request.requestId}|handler=spot`
    );
    const controller = new AbortController();
    const cancelTimer = setTimeout(() => controller.abort(), request.cancelAfterMs);
    try {
      const call = spot.context.outbound
        .requestToChannel(YieldDispatchNames.delayChannel, {
          requestId: request.requestId,
          delayMs: request.delayMs,
          marker: 'cancel'
        } satisfies DelayReq)
        .packetName('DelayReq')
        .timeout(5000);
      this.evidence.add(
        `cancel-yield-released|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${request.requestId}|handler=spot`
      );
      await call.yield<DelayRes>(controller.signal);
      this.evidence.add(
        `cancel-yield-unexpected-resumed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${request.requestId}|handler=spot`
      );
    } catch (error) {
      const errorName = error instanceof Error ? error.name : 'Error';
      this.evidence.add(
        `cancel-yield-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${request.requestId}|error=${errorName}|handler=spot`
      );
    } finally {
      clearTimeout(cancelTimer);
    }
  }
}
