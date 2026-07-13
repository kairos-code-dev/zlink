import { Injectable, Scope } from '@nestjs/common';
import type { ZLinkMessage, ZLinkSpot, ZLinkSpotActorJoinResponse, ZLinkSpotContext } from '@zlink-systems/framework';
import type { DelayReq } from '../../../Shared/messages';
import { EvidenceStore } from '../Support/evidence-store';
import { HoldCommandHandler, ProbeCommandHandler, WorkerAwaitCommandHandler, AwaitCommandHandler, AwaitRequestHandler } from '../Handlers/basic-spot-handlers';
import { AwaitCancelCommandHandler, AwaitTimeoutCommandHandler } from '../Handlers/failure-spot-handlers';
import { RemoteSpotAwaitCommandHandler, RemoteSpotAwaitHandler } from '../Handlers/remote-spot-handlers';
import { TimerStartCommandHandler, TimerStopCommandHandler } from '../Handlers/timer-spot-handlers';
import { SpotActorFastHandler, SpotActorFastSendHandler, SpotActorPushAwaitHandler, SpotActorAwaitHandler, AwaitActor } from './await-actors';
import { AwaitTimerState } from './await-timer-state';

@Injectable({ scope: Scope.TRANSIENT })
export class AwaitProbeSpot implements ZLinkSpot<AwaitActor> {
  readonly context!: ZLinkSpotContext<AwaitActor>;
  private readonly timers = new Map<string, AwaitTimerState>();

  constructor(private readonly evidence: EvidenceStore) {}

  configure(): void {
    this.context.handlers.addPacket(HoldCommandHandler);
    this.context.handlers.addPacket(AwaitCommandHandler);
    this.context.handlers.addPacket(AwaitRequestHandler);
    this.context.handlers.addPacket(WorkerAwaitCommandHandler);
    this.context.handlers.addPacket(AwaitTimeoutCommandHandler);
    this.context.handlers.addPacket(AwaitCancelCommandHandler);
    this.context.handlers.addPacket(ProbeCommandHandler);
    this.context.handlers.addPacket(RemoteSpotAwaitHandler);
    this.context.handlers.addPacket(RemoteSpotAwaitCommandHandler);
    this.context.handlers.addPacket(TimerStartCommandHandler);
    this.context.handlers.addPacket(TimerStopCommandHandler);
    this.context.handlers.addActorPacket(SpotActorAwaitHandler, AwaitActor);
    this.context.handlers.addActorPacket(SpotActorFastSendHandler, AwaitActor);
    this.context.handlers.addActorPacket(SpotActorFastHandler, AwaitActor);
    this.context.handlers.addActorPacket(SpotActorPushAwaitHandler, AwaitActor);
  }

  async onActorJoin(actorId: string, request: ZLinkMessage): Promise<ZLinkSpotActorJoinResponse> {
    const delay = request.decode<DelayReq>(Object as never);
    if (delay.delayMs > 0) {
      await new Promise<void>((resolve, reject) => {
        const timeout = setTimeout(resolve, delay.delayMs);
        void reject;
      });
    }
    this.evidence.add(`actor-admitted|rid=${this.evidence.rid}|spot=${this.context.spotRid}|actor=${actorId}`);
    return { accepted: true, reply: delay };
  }

  async onJoinedActor(actor: AwaitActor): Promise<void> {
    void actor;
  }

  async onLeaveActor(actor: AwaitActor): Promise<void> {
    void actor;
  }

  tryAddTimerState(state: AwaitTimerState): boolean {
    if (this.timers.has(state.timerName)) {
      return false;
    }
    this.timers.set(state.timerName, state);
    return true;
  }

  findTimerState(timerName: string): AwaitTimerState | undefined {
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
