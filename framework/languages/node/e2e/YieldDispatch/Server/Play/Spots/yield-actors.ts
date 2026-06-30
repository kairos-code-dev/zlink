import { Injectable } from '@nestjs/common';
import type {
  ActorJoinYieldReq,
  ActorPushNotify,
  ActorPushYieldReq,
  ActorYieldRes,
  ActorFastMsg,
  ActorFastReq,
  ActorYieldReq,
  DelayRes,
  DelayReq
} from '../../../Shared/messages';
import { YieldDispatchNames } from '../../../Shared/messages';
import type {
  ZLinkActor,
  ZLinkActorContext,
  ZLinkActorFactory,
  ZLinkEntrySpot,
  ZLinkEntrySpotActorRequestHandler,
  ZLinkEntrySpotActorSendHandler,
  ZLinkEntrySpotContext,
  ZLinkSpot,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorSendHandler,
  ZLinkSpotActorSendContext
} from '@zlink-systems/framework';
import { EvidenceStore } from '../../Support/evidence-store';
import type { YieldProbeSpot } from './yield-probe-spot';

export class YieldActor implements ZLinkActor {
  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {}
}

@Injectable()
export class YieldActorFactory implements ZLinkActorFactory {
  create(actorId: string, context: ZLinkActorContext): YieldActor {
    return new YieldActor(actorId, context);
  }
}

@Injectable()
export class YieldEntrySpot implements ZLinkEntrySpot<YieldActor> {
  readonly context!: ZLinkEntrySpotContext<YieldActor>;

  configure(): void {
    this.context.handlers.actorRequest('ActorYieldReq', EntryActorYieldHandler, YieldActor);
    this.context.handlers.actorSend('ActorFastMsg', EntryActorFastSendHandler, YieldActor);
    this.context.handlers.actorRequest('ActorFastReq', EntryActorFastHandler, YieldActor);
    this.context.handlers.actorRequest('ActorJoinYieldReq', EntryActorJoinYieldHandler, YieldActor);
    this.context.handlers.actorRequest('ActorPushYieldReq', EntryActorPushYieldHandler, YieldActor);
  }

  async onActorJoin(actor: YieldActor): Promise<ZLinkSpotActorJoinResponse> {
    void actor;
    return { accepted: true };
  }
}

@Injectable()
export class EntryActorYieldHandler
  implements ZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorYieldReq, ActorYieldRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    entrySpot: YieldEntrySpot,
    actor: YieldActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorYieldReq
  ): Promise<ActorYieldRes> {
    void context;
    await recordActorYieldEvidence(this.evidence, entrySpot, actor, request, false);
    return actorReply('YD-B', request.requestId, actor, entrySpot, 'actor-yield-completed');
  }
}

@Injectable()
export class SpotActorYieldHandler
  implements ZLinkSpotActorRequestHandler<YieldProbeSpot, YieldActor, ActorYieldReq, ActorYieldRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    actor: YieldActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorYieldReq
  ): Promise<ActorYieldRes> {
    void context;
    await recordActorYieldEvidence(this.evidence, spot, actor, request, true);
    return actorReply('YD-B', request.requestId, actor, spot, 'actor-yield-completed');
  }
}

@Injectable()
export class EntryActorFastHandler
  implements ZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorFastReq, ActorYieldRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    entrySpot: YieldEntrySpot,
    actor: YieldActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorFastReq
  ): Promise<ActorYieldRes> {
    void context;
    recordActorFastEvidence(this.evidence, entrySpot, actor, request);
    return actorReply('YD-B', request.requestId, actor, entrySpot, request.marker);
  }
}

@Injectable()
export class EntryActorFastSendHandler
  implements ZLinkEntrySpotActorSendHandler<YieldEntrySpot, YieldActor, ActorFastMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    entrySpot: YieldEntrySpot,
    actor: YieldActor,
    context: ZLinkSpotActorSendContext,
    request: ActorFastMsg
  ): Promise<void> {
    void context;
    recordActorFastEvidence(this.evidence, entrySpot, actor, request);
  }
}

@Injectable()
export class SpotActorFastHandler
  implements ZLinkSpotActorRequestHandler<YieldProbeSpot, YieldActor, ActorFastReq, ActorYieldRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    actor: YieldActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorFastReq
  ): Promise<ActorYieldRes> {
    void context;
    recordActorFastEvidence(this.evidence, spot, actor, request);
    return actorReply('YD-B', request.requestId, actor, spot, request.marker);
  }
}

@Injectable()
export class SpotActorFastSendHandler
  implements ZLinkSpotActorSendHandler<YieldProbeSpot, YieldActor, ActorFastMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    actor: YieldActor,
    context: ZLinkSpotActorSendContext,
    request: ActorFastMsg
  ): Promise<void> {
    void context;
    recordActorFastEvidence(this.evidence, spot, actor, request);
  }
}

@Injectable()
export class EntryActorPushYieldHandler
  implements ZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorPushYieldReq, ActorYieldRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    entrySpot: YieldEntrySpot,
    actor: YieldActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushYieldReq
  ): Promise<ActorYieldRes> {
    void context;
    await recordActorPushYieldEvidence(this.evidence, entrySpot, actor, request, false);
    return actorReply('YD-D4', request.requestId, actor, entrySpot, 'actor-push-yield-completed');
  }
}

@Injectable()
export class SpotActorPushYieldHandler
  implements ZLinkSpotActorRequestHandler<YieldProbeSpot, YieldActor, ActorPushYieldReq, ActorYieldRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    spot: YieldProbeSpot,
    actor: YieldActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushYieldReq
  ): Promise<ActorYieldRes> {
    void context;
    await recordActorPushYieldEvidence(this.evidence, spot, actor, request, true);
    return actorReply('YD-D4', request.requestId, actor, spot, 'actor-push-yield-completed');
  }
}

@Injectable()
export class EntryActorJoinYieldHandler
  implements ZLinkEntrySpotActorRequestHandler<YieldEntrySpot, YieldActor, ActorJoinYieldReq, ActorYieldRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  async handle(
    entrySpot: YieldEntrySpot,
    actor: YieldActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorJoinYieldReq
  ): Promise<ActorYieldRes> {
    void context;
    const mailboxId = `actor:${actor.actorId}`;
    this.evidence.add(
      `actor-join-yield-started|rid=${this.evidence.rid}|spot=${entrySpot.context.spotRid}`
      + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|target=${request.targetSpotRid}`
    );
    const call = actor.context.joinSpot(request.targetSpotRid, {
      requestId: request.requestId,
      delayMs: 350,
      marker: 'join'
    } satisfies DelayReq);
    this.evidence.add(
      `actor-join-yield-released|rid=${this.evidence.rid}|spot=${entrySpot.context.spotRid}`
      + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|target=${request.targetSpotRid}`
    );
    const joined = await call.submit<DelayRes>();
    this.evidence.add(
      `actor-join-yield-resumed|rid=${this.evidence.rid}|spot=${entrySpot.context.spotRid}`
      + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|accepted=${joined.resultCode === 0}`
    );
    this.evidence.add(
      `actor-join-yield-completed|rid=${this.evidence.rid}|spot=${entrySpot.context.spotRid}`
      + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|accepted=${joined.resultCode === 0}`
    );
    return actorReply('YD-B3', request.requestId, actor, entrySpot, 'actor-join-yield-completed');
  }
}

type ActorEvidenceTarget = (ZLinkEntrySpot<YieldActor> | ZLinkSpot<YieldActor>) & {
  readonly context: NonNullable<ZLinkEntrySpot<YieldActor>['context']> | NonNullable<ZLinkSpot<YieldActor>['context']>;
};

async function recordActorYieldEvidence(
  evidence: EvidenceStore,
  target: ActorEvidenceTarget,
  actor: YieldActor,
  request: ActorYieldReq,
  useYield: boolean
): Promise<void> {
  const mailboxId = `actor:${actor.actorId}`;
  evidence.add(
    `actor-yield-started|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  const call = target.context.outbound
    .requestToChannel(YieldDispatchNames.delayChannel, {
      requestId: request.requestId,
      delayMs: request.delayMs,
      marker: `actor-${actor.actorId}`
    } satisfies DelayReq)
    .packetName('DelayReq')
    .timeout(5000);
  evidence.add(
    `actor-yield-released|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  if (useYield) {
    await call.yield<DelayRes>();
  } else {
    await call.submit<DelayRes>();
  }
  evidence.add(
    `actor-yield-resumed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  evidence.add(
    `actor-yield-completed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
}

async function recordActorPushYieldEvidence(
  evidence: EvidenceStore,
  target: ActorEvidenceTarget,
  actor: YieldActor,
  request: ActorPushYieldReq,
  useYield: boolean
): Promise<void> {
  const mailboxId = `actor:${actor.actorId}`;
  evidence.add(
    `actor-push-yield-started|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  const call = target.context.outbound
    .requestToChannel(YieldDispatchNames.delayChannel, {
      requestId: request.requestId,
      delayMs: request.delayMs,
      marker: `actor-push-${actor.actorId}`
    } satisfies DelayReq)
    .packetName('DelayReq')
    .timeout(5000);
  evidence.add(
    `actor-push-yield-released|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  if (useYield) {
    await call.yield<DelayRes>();
  } else {
    await call.submit<DelayRes>();
  }
  evidence.add(
    `actor-push-yield-resumed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  await actor.context.boundSession
    .send({
      actorId: actor.actorId,
      requestId: request.requestId,
      value: request.value,
      nodeRid: String(target.context.nodeRid)
    } satisfies ActorPushNotify)
    .packetName('ActorPushNotify')
    .submit();
  evidence.add(
    `actor-push-yield-completed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
}

function recordActorFastEvidence(
  evidence: EvidenceStore,
  target: ActorEvidenceTarget,
  actor: YieldActor,
  request: ActorFastReq | ActorFastMsg
): void {
  const mailboxId = `actor:${actor.actorId}`;
  evidence.add(
    `actor-fast-started|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}`
    + `|marker=${request.marker}|handler=actor`
  );
  evidence.add(
    `actor-fast-completed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}`
    + `|marker=${request.marker}|handler=actor`
  );
}

function actorReply(
  scenarioId: string,
  requestId: string,
  actor: YieldActor,
  target: ActorEvidenceTarget,
  marker: string
): ActorYieldRes {
  return {
    scenarioId,
    requestId,
    actorId: actor.actorId,
    spotRid: String(target.context.spotRid),
    nodeRid: String(target.context.nodeRid),
    marker
  };
}
