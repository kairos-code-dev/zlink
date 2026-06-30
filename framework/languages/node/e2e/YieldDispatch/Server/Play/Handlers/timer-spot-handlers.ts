import { Injectable } from '@nestjs/common';
import type { ZLinkHandlerContext, ZLinkSpotPacketHandler, ZLinkSpotTimerHandler, ZLinkTimerTick } from '@zlink-systems/framework';
import { ZLinkTimerOverrunPolicy } from '@zlink-systems/framework';
import type { DelayRes, DelayReq, TimerStartMsg, TimerStopMsg } from '../../../Shared/messages';
import { YieldDispatchNames } from '../../../Shared/messages';
import { EvidenceStore } from '../../Support/evidence-store';
import type { YieldProbeSpot } from '../Spots/yield-probe-spot';
import { YieldTimerState } from '../Spots/yield-timer-state';

@Injectable()
export class TimerStartCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, TimerStartMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: YieldProbeSpot, request: TimerStartMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    const state = new YieldTimerState(request.requestId, request.timerName, request.mode, request.delayMs);
    if (!spot.tryAddTimerState(state)) {
      this.evidence.add(
        `timer-start-duplicate-ignored|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${request.requestId}|timer=${request.timerName}|mode=${request.mode}`
      );
      return;
    }

    state.timer = await spot.context.addTimer(
      request.timerName,
      request.periodMs,
      YieldTimerHandler,
      { overrunPolicy: ZLinkTimerOverrunPolicy.DelayNextTick }
    );
    this.evidence.add(
      `timer-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
      + `|request=${request.requestId}|timer=${request.timerName}|mode=${request.mode}`
    );
  }
}

@Injectable()
export class TimerStopCommandHandler implements ZLinkSpotPacketHandler<YieldProbeSpot, TimerStopMsg> {
  async handle(spot: YieldProbeSpot, request: TimerStopMsg, context: ZLinkHandlerContext): Promise<void> {
    void context;
    await spot.stopScenarioTimers(request.requestId);
  }
}

@Injectable()
export class YieldTimerHandler implements ZLinkSpotTimerHandler<YieldProbeSpot> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(spot: YieldProbeSpot, tick: ZLinkTimerTick): Promise<void> {
    const state = spot.findTimerState(tick.name);
    if (state === undefined) {
      return;
    }
    const tickNumber = state.nextTick();
    if (state.mode === 'fast') {
      this.evidence.add(
        `timer-fast-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${state.requestId}|timer=${state.timerName}|tick=${tickNumber}|handler=timer`
      );
      this.evidence.add(
        `timer-fast-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${state.requestId}|timer=${state.timerName}|tick=${tickNumber}|handler=timer`
      );
      return;
    }

    if (tickNumber === 1 && (state.mode === 'yield-on-first' || state.mode === 'yield-then-next')) {
      this.evidence.add(
        `timer-yield-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${state.requestId}|timer=${state.timerName}|tick=${tickNumber}|handler=timer`
      );
      const call = spot.context.outbound
        .requestToChannel(YieldDispatchNames.delayChannel, {
          requestId: state.requestId,
          delayMs: state.delayMs,
          marker: state.timerName
        } satisfies DelayReq)
        .packetName('DelayReq')
        .timeout(5000);
      this.evidence.add(
        `timer-yield-released|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${state.requestId}|timer=${state.timerName}|tick=${tickNumber}|handler=timer`
      );
      await call.yield<DelayRes>();
      this.evidence.add(
        `timer-yield-resumed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${state.requestId}|timer=${state.timerName}|tick=${tickNumber}|handler=timer`
      );
      this.evidence.add(
        `timer-yield-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${state.requestId}|timer=${state.timerName}|tick=${tickNumber}|handler=timer`
      );
      return;
    }

    if (state.mode === 'yield-then-next' && tickNumber === 2) {
      this.evidence.add(
        `timer-next-started|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${state.requestId}|timer=${state.timerName}|tick=${tickNumber}|handler=timer`
      );
      this.evidence.add(
        `timer-next-completed|rid=${this.evidence.rid}|spot=${spot.context.spotRid}`
        + `|request=${state.requestId}|timer=${state.timerName}|tick=${tickNumber}|handler=timer`
      );
    }
  }
}
