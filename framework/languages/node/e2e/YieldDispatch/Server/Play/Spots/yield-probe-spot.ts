import { Injectable, Scope } from '@nestjs/common';
import type { ZLinkMessage, ZLinkSpot, ZLinkSpotActorJoinResponse, ZLinkSpotContext } from '@zlink-systems/framework';
import type { DelayReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Support/evidence-store';
import { HoldCommandHandler, ProbeCommandHandler, WorkerYieldCommandHandler, YieldCommandHandler, YieldRequestHandler } from '../Handlers/basic-spot-handlers';
import { YieldCancelCommandHandler, YieldTimeoutCommandHandler } from '../Handlers/failure-spot-handlers';
import { RemoteSpotYieldCommandHandler, RemoteSpotYieldHandler } from '../Handlers/remote-spot-handlers';
import { TimerStartCommandHandler, TimerStopCommandHandler } from '../Handlers/timer-spot-handlers';
import { SpotActorFastHandler, SpotActorFastSendHandler, SpotActorPushYieldHandler, SpotActorYieldHandler, type YieldActor } from './yield-actors';
import { YieldTimerState } from './yield-timer-state';

@Injectable({ scope: Scope.TRANSIENT })
export class YieldProbeSpot implements ZLinkSpot<YieldActor> {
  readonly context!: ZLinkSpotContext<YieldActor>;
  private readonly timers = new Map<string, YieldTimerState>();

  constructor(private readonly evidence: EvidenceStore) {}

  configure(): void {
    this.context.handlers.packet('HoldMsg', HoldCommandHandler);
    this.context.handlers.packet('YieldMsg', YieldCommandHandler);
    this.context.handlers.packet('YieldReq', YieldRequestHandler);
    this.context.handlers.packet('WorkerYieldMsg', WorkerYieldCommandHandler);
    this.context.handlers.packet('YieldTimeoutMsg', YieldTimeoutCommandHandler);
    this.context.handlers.packet('YieldCancelMsg', YieldCancelCommandHandler);
    this.context.handlers.packet('ProbeMsg', ProbeCommandHandler);
    this.context.handlers.packet('RemoteSpotYieldReq', RemoteSpotYieldHandler);
    this.context.handlers.packet('RemoteSpotYieldMsg', RemoteSpotYieldCommandHandler);
    this.context.handlers.packet('TimerStartMsg', TimerStartCommandHandler);
    this.context.handlers.packet('TimerStopMsg', TimerStopCommandHandler);
    this.context.handlers.actorRequest('ActorYieldReq', SpotActorYieldHandler);
    this.context.handlers.actorSend('ActorFastMsg', SpotActorFastSendHandler);
    this.context.handlers.actorRequest('ActorFastReq', SpotActorFastHandler);
    this.context.handlers.actorRequest('ActorPushYieldReq', SpotActorPushYieldHandler);
  }

  async onActorJoin(actorId: string, request: ZLinkMessage, signal?: AbortSignal): Promise<ZLinkSpotActorJoinResponse> {
    const delay = request.decode<DelayReq>(Object as never);
    if (delay.delayMs > 0) {
      await new Promise<void>((resolve, reject) => {
        const timeout = setTimeout(resolve, delay.delayMs);
        signal?.addEventListener('abort', () => {
          clearTimeout(timeout);
          reject(new Error('Actor join was canceled.'));
        }, { once: true });
      });
    }
    this.evidence.add(`actor-admitted|rid=${this.evidence.rid}|spot=${this.context.spotRid}|actor=${actorId}`);
    return { accepted: true, reply: delay };
  }

  tryAddTimerState(state: YieldTimerState): boolean {
    if (this.timers.has(state.timerName)) {
      return false;
    }
    this.timers.set(state.timerName, state);
    return true;
  }

  findTimerState(timerName: string): YieldTimerState | undefined {
    return this.timers.get(timerName);
  }

  async stopScenarioTimers(requestId: string): Promise<void> {
    const matches = [...this.timers.values()].filter((state) => state.requestId === requestId);
    for (const state of matches) {
      this.timers.delete(state.timerName);
    }
    await Promise.all(matches.map((state) => state.timer?.cancel()));
  }
}
