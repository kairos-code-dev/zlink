import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT } from '@zlink-systems/nestjs';
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
  ZLinkActorJoinRequest,
  ZLinkActorMembership,
  ZLinkChannelClient,
  ZLinkEntrySpot,
  ZLinkEntrySpotContext,
  ZLinkSpotActorJoinResponse,
  ZLinkSpotActorRequestHandler,
  ZLinkSpotActorRequestContext,
  ZLinkSpotActorSendHandler,
  ZLinkSpotActorSendContext
} from '@zlink-systems/framework';
import { ZLinkSpotActorRequest, ZLinkSpotActorSend } from '@zlink-systems/framework';
import { EvidenceStore } from '../Support/evidence-store';

export class AwaitActor implements ZLinkActor {
  constructor(readonly actorId: string, readonly context: ZLinkActorContext) {}

  configure(): void {
    this.context.handlers.addHandler(EntryActorAwaitHandler);
    this.context.handlers.addHandler(EntryActorFastSendHandler);
    this.context.handlers.addHandler(EntryActorFastHandler);
    this.context.handlers.addHandler(EntryActorJoinAwaitHandler);
    this.context.handlers.addHandler(EntryActorPushAwaitHandler);
  }
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

  async onActorJoin(actor: ZLinkActorJoinRequest): Promise<ZLinkSpotActorJoinResponse> {
    void actor;
    return { accepted: true };
  }

  async onJoinedActor(actor: ZLinkActorMembership): Promise<void> { void actor; }

  async onLeaveActor(actor: ZLinkActorMembership): Promise<void> { void actor; }

  async onDisconnectActor(actor: ZLinkActorMembership): Promise<void> { void actor; }
}

@Injectable()
export class EntryActorAwaitHandler
{
  constructor(
    private readonly evidence: EvidenceStore,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient
  ) {}

  @ZLinkSpotActorRequest('ActorAwaitReq')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    const target = actorEvidenceTarget(this.evidence, actor);
    await recordActorAwaitEvidence(this.evidence, this.channels, target, actor, request);
    return actorReply('TD-D', request.requestId, actor, target, 'actor-await-completed');
  }
}

@Injectable()
export class SpotActorAwaitHandler
  implements ZLinkSpotActorRequestHandler<AwaitActor, ActorAwaitReq, ActorAwaitRes> {
  constructor(
    private readonly evidence: EvidenceStore,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient
  ) {}

  @ZLinkSpotActorRequest('ActorAwaitReq')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    const target = actorEvidenceTarget(this.evidence, actor);
    await recordActorAwaitEvidence(this.evidence, this.channels, target, actor, request);
    return actorReply('TD-D', request.requestId, actor, target, 'actor-await-completed');
  }
}

@Injectable()
export class EntryActorFastHandler
{
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorFastReq')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorFastReq
  ): Promise<ActorAwaitRes> {
    void context;
    const target = actorEvidenceTarget(this.evidence, actor);
    recordActorFastEvidence(this.evidence, target, actor, request);
    return actorReply('TD-D', request.requestId, actor, target, request.marker);
  }
}

@Injectable()
export class EntryActorFastSendHandler
{
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorSend('ActorFastMsg')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorSendContext,
    request: ActorFastMsg
  ): Promise<void> {
    void context;
    recordActorFastEvidence(this.evidence, actorEvidenceTarget(this.evidence, actor), actor, request);
  }
}

@Injectable()
export class SpotActorFastHandler
  implements ZLinkSpotActorRequestHandler<AwaitActor, ActorFastReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorFastReq')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorFastReq
  ): Promise<ActorAwaitRes> {
    void context;
    const target = actorEvidenceTarget(this.evidence, actor);
    recordActorFastEvidence(this.evidence, target, actor, request);
    return actorReply('TD-D', request.requestId, actor, target, request.marker);
  }
}

@Injectable()
export class SpotActorFastSendHandler
  implements ZLinkSpotActorSendHandler<AwaitActor, ActorFastMsg> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorSend('ActorFastMsg')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorSendContext,
    request: ActorFastMsg
  ): Promise<void> {
    void context;
    recordActorFastEvidence(this.evidence, actorEvidenceTarget(this.evidence, actor), actor, request);
  }
}

@Injectable()
export class EntryActorPushAwaitHandler
{
  constructor(
    private readonly evidence: EvidenceStore,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient
  ) {}

  @ZLinkSpotActorRequest('ActorPushAwaitReq')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    const target = actorEvidenceTarget(this.evidence, actor);
    await recordActorPushAwaitEvidence(this.evidence, this.channels, target, actor, request, false);
    return actorReply('TD-F3', request.requestId, actor, target, 'actor-push-await-completed');
  }
}

@Injectable()
export class SpotActorPushAwaitHandler
  implements ZLinkSpotActorRequestHandler<AwaitActor, ActorPushAwaitReq, ActorAwaitRes> {
  constructor(
    private readonly evidence: EvidenceStore,
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient
  ) {}

  @ZLinkSpotActorRequest('ActorPushAwaitReq')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorPushAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    const target = actorEvidenceTarget(this.evidence, actor);
    await recordActorPushAwaitEvidence(this.evidence, this.channels, target, actor, request, true);
    return actorReply('TD-F3', request.requestId, actor, target, 'actor-push-await-completed');
  }
}

@Injectable()
export class EntryActorJoinAwaitHandler
{
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorJoinAwaitReq')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorJoinAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    const target = actorEvidenceTarget(this.evidence, actor);
    await recordActorJoinEvidence(this.evidence, target, actor, request, 'actor-join-await-completed');
    return actorReply('TD-E1', request.requestId, actor, target, 'actor-join-await-completed');
  }
}

@Injectable()
export class SpotActorJoinAwaitHandler
  implements ZLinkSpotActorRequestHandler<AwaitActor, ActorJoinAwaitReq, ActorAwaitRes> {
  constructor(private readonly evidence: EvidenceStore) {}

  @ZLinkSpotActorRequest('ActorJoinAwaitReq')
  async handle(
    actor: AwaitActor,
    context: ZLinkSpotActorRequestContext,
    request: ActorJoinAwaitReq
  ): Promise<ActorAwaitRes> {
    void context;
    const target = actorEvidenceTarget(this.evidence, actor);
    await recordActorJoinEvidence(this.evidence, target, actor, request, 'actor-join-completed');
    return actorReply('TD-E', request.requestId, actor, target, 'actor-join-completed');
  }
}

interface ActorEvidenceTarget {
  readonly spotRid: unknown;
  readonly nodeRid: unknown;
}

function actorEvidenceTarget(evidence: EvidenceStore, actor: AwaitActor): ActorEvidenceTarget {
  return {
    spotRid: actor.context.spotRid ?? evidence.rid,
    nodeRid: evidence.rid
  };
}

async function recordActorAwaitEvidence(
  evidence: EvidenceStore,
  channels: ZLinkChannelClient,
  target: ActorEvidenceTarget,
  actor: AwaitActor,
  request: ActorAwaitReq
): Promise<void> {
  const terminator = request.terminator ?? 'async';
  const mailboxId = `actor:${actor.actorId}`;
  evidence.add(
    `actor-await-started|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  const call = channels
    .requestToChannel(AutomaticTurnDispatchNames.delayChannel,
      new DelayReq(request.requestId, request.delayMs, `actor-${actor.actorId}`))
    .timeout(5000);
  evidence.add(
    `actor-await-${terminator === 'yield' ? 'released' : 'held'}|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  if (terminator === 'yield') {
    await call.yield<DelayRes>();
  } else {
    await call.submit<DelayRes>();
  }
  evidence.add(
    `actor-await-resumed|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  evidence.add(
    `actor-await-completed|rid=${evidence.rid}|spot=${target.spotRid}`
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
    `actor-join-started|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|target=${request.targetSpotRid}`
  );
  const joined = await actor.context
    .joinSpot(request.targetSpotRid, new DelayReq(request.requestId, 0, 'join'))
    .submit<DelayRes>();
  evidence.add(
    `${completedMarker}|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}`
    + `|target=${request.targetSpotRid}|accepted=${joined.status === 'accepted'}`
  );
}

async function recordActorPushAwaitEvidence(
  evidence: EvidenceStore,
  channels: ZLinkChannelClient,
  target: ActorEvidenceTarget,
  actor: AwaitActor,
  request: ActorPushAwaitReq,
  useAwait: boolean
): Promise<void> {
  const mailboxId = `actor:${actor.actorId}`;
  evidence.add(
    `actor-push-await-started|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  const call = channels
    .requestToChannel(AutomaticTurnDispatchNames.delayChannel,
      new DelayReq(request.requestId, request.delayMs, `actor-push-${actor.actorId}`))
    .timeout(5000);
  evidence.add(
    `actor-push-await-released|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  if (useAwait) {
    await call.submit<DelayRes>();
  } else {
    await call.submit<DelayRes>();
  }
  evidence.add(
    `actor-push-await-resumed|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}|handler=actor`
  );
  actor.context.boundSession
    .send(new ActorPushNotify(
      actor.actorId,
      request.requestId,
      request.value,
      String(target.nodeRid)
    ))
    .submit();
  evidence.add(
    `actor-push-await-completed|rid=${evidence.rid}|spot=${target.spotRid}`
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
    `actor-fast-started|rid=${evidence.rid}|spot=${target.spotRid}`
    + `|actor=${actor.actorId}|mailbox=${mailboxId}|request=${request.requestId}`
    + `|marker=${request.marker}|handler=actor`
  );
  evidence.add(
    `actor-fast-completed|rid=${evidence.rid}|spot=${target.spotRid}`
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
    spotRid: String(target.spotRid),
    nodeRid: String(target.nodeRid),
    marker
  };
}
