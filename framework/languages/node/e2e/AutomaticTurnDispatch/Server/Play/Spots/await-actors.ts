import { Injectable } from '@nestjs/common';
import { DelayReq } from '../../../Shared/messages';
import type {
  ActorJoinAwaitReq,
  ActorPushAwaitReq,
  ActorAwaitRes,
  ActorFastMsg,
  ActorFastReq,
  ActorAwaitReq,
  DelayRes
} from '../../../Shared/messages';
import { ActorPushNotify, AutomaticTurnDispatchNames } from '../../../Shared/messages';
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
import { ZLinkSpotActorRequest, ZLinkSpotActorSend } from '@zlink-systems/framework';
import { EvidenceStore } from '../Support/evidence-store';
import type { AwaitProbeSpot } from './await-probe-spot';

export class AwaitActor implements ZLinkActor {
  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {}
}

@Injectable()
export class AwaitActorFactory implements ZLinkActorFactory {
  async create(actorId: string, context: ZLinkActorContext): Promise<AwaitActor> {
    return new AwaitActor(actorId, context);
  }
}

@Injectable()
export class AwaitEntrySpot implements ZLinkEntrySpot<AwaitActor> {
  readonly context!: ZLinkEntrySpotContext<AwaitActor>;

  configure(): void {
    this.context.handlers.addActorPacket(EntryActorAwaitHandler, AwaitActor);
    this.context.handlers.addActorPacket(EntryActorFastSendHandler, AwaitActor);
    this.context.handlers.addActorPacket(EntryActorFastHandler, AwaitActor);
    this.context.handlers.addActorPacket(EntryActorJoinAwaitHandler, AwaitActor);
    this.context.handlers.addActorPacket(EntryActorPushAwaitHandler, AwaitActor);
  }

  async onActorJoin(actorId: string): Promise<ZLinkSpotActorJoinResponse> {
    void actorId;
    return { accepted: true };
  }

  async onJoinedActor(actor: AwaitActor): Promise<void> { void actor; }

  async onLeaveActor(actor: AwaitActor): Promise<void> { void actor; }
}

@Injectable()
export class EntryActorAwaitHandler
  implements ZLinkEntrySpotActorRequestHandler<AwaitEntrySpot, AwaitActor, ActorAwaitReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorAwaitReq')
  async handle(
    entrySpot: AwaitEntrySpot,
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    await recordActorAwaitEvidence(this.evidence, entrySpot, actor, request);
    return actorReply('TD-D', request.requestId, actor, entrySpot, 'actor-await-completed');
  }
}

@Injectable()
export class SpotActorAwaitHandler
  implements ZLinkSpotActorRequestHandler<AwaitProbeSpot, AwaitActor, ActorAwaitReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorAwaitReq')
  async handle(
    spot: AwaitProbeSpot,
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    await recordActorAwaitEvidence(this.evidence, spot, actor, request);
    return actorReply('TD-D', request.requestId, actor, spot, 'actor-await-completed');
  }
}

@Injectable()
export class EntryActorFastHandler
  implements ZLinkEntrySpotActorRequestHandler<AwaitEntrySpot, AwaitActor, ActorFastReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorFastReq')
  async handle(
    entrySpot: AwaitEntrySpot,
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorFastReq
  ): Promise<ActorAwaitRes> {
    void context;
    recordActorFastEvidence(this.evidence, entrySpot, actor, request);
    return actorReply('TD-D', request.requestId, actor, entrySpot, request.marker);
  }
}

@Injectable()
export class EntryActorFastSendHandler
  implements ZLinkEntrySpotActorSendHandler<AwaitEntrySpot, AwaitActor, ActorFastMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorSend('ActorFastMsg')
  async handle(
    entrySpot: AwaitEntrySpot,
    actor: AwaitActor,
    context: ZLinkSpotActorSendContext,
    request: ActorFastMsg
  ): Promise<void> {
    void context;
    recordActorFastEvidence(this.evidence, entrySpot, actor, request);
  }
}

@Injectable()
export class SpotActorFastHandler
  implements ZLinkSpotActorRequestHandler<AwaitProbeSpot, AwaitActor, ActorFastReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorFastReq')
  async handle(
    spot: AwaitProbeSpot,
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorFastReq
  ): Promise<ActorAwaitRes> {
    void context;
    recordActorFastEvidence(this.evidence, spot, actor, request);
    return actorReply('TD-D', request.requestId, actor, spot, request.marker);
  }
}

@Injectable()
export class SpotActorFastSendHandler
  implements ZLinkSpotActorSendHandler<AwaitProbeSpot, AwaitActor, ActorFastMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorSend('ActorFastMsg')
  async handle(
    spot: AwaitProbeSpot,
    actor: AwaitActor,
    context: ZLinkSpotActorSendContext,
    request: ActorFastMsg
  ): Promise<void> {
    void context;
    recordActorFastEvidence(this.evidence, spot, actor, request);
  }
}

@Injectable()
export class EntryActorPushAwaitHandler
  implements ZLinkEntrySpotActorRequestHandler<AwaitEntrySpot, AwaitActor, ActorPushAwaitReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorPushAwaitReq')
  async handle(
    entrySpot: AwaitEntrySpot,
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    await recordActorPushAwaitEvidence(this.evidence, entrySpot, actor, request, false);
    return actorReply('TD-F3', request.requestId, actor, entrySpot, 'actor-push-await-completed');
  }
}

@Injectable()
export class SpotActorPushAwaitHandler
  implements ZLinkSpotActorRequestHandler<AwaitProbeSpot, AwaitActor, ActorPushAwaitReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorPushAwaitReq')
  async handle(
    spot: AwaitProbeSpot,
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    await recordActorPushAwaitEvidence(this.evidence, spot, actor, request, true);
    return actorReply('TD-F3', request.requestId, actor, spot, 'actor-push-await-completed');
  }
}

@Injectable()
export class EntryActorJoinAwaitHandler
  implements ZLinkEntrySpotActorRequestHandler<AwaitEntrySpot, AwaitActor, ActorJoinAwaitReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorJoinAwaitReq')
  async handle(
    entrySpot: AwaitEntrySpot,
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorJoinAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    await recordActorJoinEvidence(this.evidence, entrySpot, actor, request, 'actor-join-await-completed');
    return actorReply('TD-E1', request.requestId, actor, entrySpot, 'actor-join-await-completed');
  }
}

@Injectable()
export class SpotActorJoinAwaitHandler
  implements ZLinkSpotActorRequestHandler<AwaitProbeSpot, AwaitActor, ActorJoinAwaitReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorJoinAwaitReq')
  async handle(
    spot: AwaitProbeSpot,
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorJoinAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    await recordActorJoinEvidence(this.evidence, spot, actor, request, 'actor-join-completed');
    return actorReply('TD-E', request.requestId, actor, spot, 'actor-join-completed');
  }
}

type ActorEvidenceTarget = (ZLinkEntrySpot<AwaitActor> | ZLinkSpot<AwaitActor>) & {
  readonly context: NonNullable<ZLinkEntrySpot<AwaitActor>['context']> | NonNullable<ZLinkSpot<AwaitActor>['context']>;
};

async function recordActorAwaitEvidence(
  evidence: EvidenceStore,
  target: ActorEvidenceTarget,
  actor: AwaitActor,
  request: ActorAwaitReq
): Promise<void> {
  const terminator = request.terminator ?? 'async';
  const mailboxId = `actor:${actor.actorId}`;
  evidence.add(
    `actor-await-started|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  const call = target.context.outbound
    .requestToChannel(AutomaticTurnDispatchNames.delayChannel,
      new DelayReq(request.requestId, request.delayMs, `actor-${actor.actorId}`))
    .timeout(5000);
  evidence.add(
    `actor-await-${terminator === 'yield' ? 'released' : 'held'}|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  if (terminator === 'yield') {
    await call.yield<DelayRes>();
  } else {
    await call.submit<DelayRes>();
  }
  evidence.add(
    `actor-await-resumed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  evidence.add(
    `actor-await-completed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
}

async function recordActorJoinEvidence(
  evidence: EvidenceStore,
  target: ActorEvidenceTarget,
  actor: AwaitActor,
  request: ActorJoinAwaitReq,
  completedMarker: string
): Promise<void> {
  const mailboxId = `actor:${actor.actorId}`;
  evidence.add(
    `actor-join-started|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|target=${request.targetSpotRid}`
  );
  const joined = await actor.context
    .joinSpot(request.targetSpotRid, new DelayReq(request.requestId, 0, 'join'))
    .submit<DelayRes>();
  evidence.add(
    `${completedMarker}|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}`
    + `|target=${request.targetSpotRid}|accepted=${joined.status === 'accepted'}`
  );
}

async function recordActorPushAwaitEvidence(
  evidence: EvidenceStore,
  target: ActorEvidenceTarget,
  actor: AwaitActor,
  request: ActorPushAwaitReq,
  useAwait: boolean
): Promise<void> {
  const mailboxId = `actor:${actor.actorId}`;
  evidence.add(
    `actor-push-await-started|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  const call = target.context.outbound
    .requestToChannel(AutomaticTurnDispatchNames.delayChannel,
      new DelayReq(request.requestId, request.delayMs, `actor-push-${actor.actorId}`))
    .timeout(5000);
  evidence.add(
    `actor-push-await-released|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  if (useAwait) {
    await call.submit<DelayRes>();
  } else {
    await call.submit<DelayRes>();
  }
  evidence.add(
    `actor-push-await-resumed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  actor.context.boundSession
    .send(new ActorPushNotify(
      actor.actorId,
      request.requestId,
      request.value,
      String(target.context.nodeRid)
    ))
    .submit();
  evidence.add(
    `actor-push-await-completed|rid=${evidence.rid}|spot=${target.context.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
}

function recordActorFastEvidence(
  evidence: EvidenceStore,
  target: ActorEvidenceTarget,
  actor: AwaitActor,
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
  actor: AwaitActor,
  target: ActorEvidenceTarget,
  marker: string
): ActorAwaitRes {
  return {
    scenarioId,
    requestId,
    actorId: actor.actorId,
    spotRid: String(target.context.spotRid),
    nodeRid: String(target.context.nodeRid),
    marker
  };
}
