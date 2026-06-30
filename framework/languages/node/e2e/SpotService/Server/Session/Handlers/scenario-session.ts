import { Inject, Injectable } from '@nestjs/common';
import type {
  AuthReply,
  AuthReq,
  EnsureActorReply,
  JoinUserSpotActorReply,
  MultiBindReply,
  MultiBindReq,
  UserSpotAuthReq
} from '../../../Shared/messages';
import { SpotServiceNames } from '../../../Shared/messages';
import type {
  ZLinkActorManager,
  ZLinkMessage,
  ZLinkRouteClient,
  ZLinkSession,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext,
  ZLinkSessionFactory
} from '@zlink-systems/framework';
import { ZLINK_ACTOR_MANAGER, ZLINK_ROUTE_CLIENT } from '@zlink-systems/nestjs';
import { EvidenceStore } from '../Infrastructure/evidence-store';

class ScenarioSession implements ZLinkSession {
  constructor(
    private readonly route: ZLinkRouteClient,
    private readonly actors: ZLinkActorManager,
    private readonly evidence: EvidenceStore,
    readonly context: ZLinkSessionContext
  ) {}

  async onConnected(): Promise<void> {
    this.evidence.add(`session-connected|rid=${this.evidence.rid}|session=${this.context.sessionId}`);
  }

  async onDisconnected(): Promise<void> {
    for (const actor of this.context.actors.bound.slice(0, 1)) {
      await actor.notifyDisconnected();
    }
    this.evidence.add(`session-disconnected|rid=${this.evidence.rid}|session=${this.context.sessionId}`);
  }

  async onDispatch(dispatch: ZLinkSessionDispatchContext, payload: ZLinkMessage, signal?: AbortSignal): Promise<void> {
    if (dispatch.packetName === 'AuthReq') {
      const request = payload.decode<AuthReq>(Object as never);
      this.evidence.add(`session-auth|rid=${this.evidence.rid}|actor=${request.actorId}|step=received`);
      try {
        const ensured = request.nodeRid === this.evidence.rid
          ? await this.ensureLocalActor(request, signal)
          : await this.route
            .request(SpotServiceNames.controlChannel, request.nodeRid, {
              actorId: request.actorId,
              displayName: request.displayName,
              nodeRid: request.nodeRid
            })
            .packetName('EnsureActorReq')
            .timeout(5000)
            .submit<EnsureActorReply>(signal);
        this.evidence.add(`session-auth|rid=${this.evidence.rid}|actor=${request.actorId}|step=ensured`);
        await this.context.actors.bind({
          actorId: ensured.actorId,
          nodeRid: ensured.nodeRid,
          generation: BigInt(ensured.generation)
        }, signal);
        this.evidence.add(`session-auth|rid=${this.evidence.rid}|actor=${request.actorId}|step=bound`);
        await this.context.client.reply({
          actorId: ensured.actorId,
          nodeRid: ensured.nodeRid
        } satisfies AuthReply).submit(signal);
        this.evidence.add(`session-auth|rid=${this.evidence.rid}|actor=${request.actorId}|step=replied`);
      } catch (error) {
        this.evidence.add(
          `session-auth|rid=${this.evidence.rid}|actor=${request.actorId}|step=failed|error=${error instanceof Error ? error.message : String(error)}`
        );
        throw error;
      }
      return;
    }

    if (dispatch.packetName === 'UserSpotAuthReq') {
      const request = payload.decode<UserSpotAuthReq>(Object as never);
      const joined = await this.joinRemoteUserSpot(request, signal);
      if (!joined.accepted) {
        throw new Error(`User spot actor join was rejected: ${joined.actorId}`);
      }
      await this.context.actors.bind({
        actorId: joined.actorId,
        nodeRid: request.nodeRid,
        generation: BigInt(joined.generation)
      }, signal);
      await this.context.client.reply({
        actorId: joined.actorId,
        nodeRid: request.nodeRid
      } satisfies AuthReply).submit(signal);
      return;
    }

    if (dispatch.packetName === 'MultiBindReq') {
      const request = payload.decode<MultiBindReq>(Object as never);
      for (const actorId of [request.firstActorId, request.secondActorId]) {
        const ensured = await this.ensureRemoteActor({
          actorId,
          displayName: actorId,
          nodeRid: request.nodeRid
        }, signal);
        await this.context.actors.bind({
          actorId: ensured.actorId,
          nodeRid: ensured.nodeRid,
          generation: BigInt(ensured.generation)
        }, signal);
      }
      await this.context.client.reply({
        boundCount: this.context.actors.bound.length
      } satisfies MultiBindReply).submit(signal);
      return;
    }

    const actorId = dispatch.metadata.get(SpotServiceNames.actorIdMetadata);
    const actor = actorId === undefined || actorId.trim() === ''
      ? this.requireSingleBoundActor(dispatch.packetName)
      : this.context.actors.find(actorId);
    if (actor === undefined) {
      throw new Error(`Actor route not found: ${actorId}`);
    }
    await actor.relay(payload, signal);
  }

  private async ensureLocalActor(request: AuthReq, signal?: AbortSignal): Promise<EnsureActorReply> {
    const actorRef = await this.actors.getOrCreate(
      request.actorId,
      SpotServiceNames.actorType,
      request,
      signal
    );
    this.evidence.add(`ensure-actor|rid=${this.evidence.rid}|actor=${request.actorId}`);
    this.evidence.add(`entry-joined|rid=${this.evidence.rid}|actor=${request.actorId}`);
    return {
      actorId: actorRef.actorId,
      nodeRid: String(actorRef.nodeRid),
      generation: actorRef.generation.toString()
    };
  }

  private async joinRemoteUserSpot(request: UserSpotAuthReq, signal?: AbortSignal): Promise<JoinUserSpotActorReply> {
    await this.route
      .request(SpotServiceNames.controlChannel, request.nodeRid, { spotRid: request.spotRid })
      .packetName('CreateSpotReq')
      .timeout(5000)
      .submit(signal);
    return await this.route
      .request(SpotServiceNames.controlChannel, request.nodeRid, {
        spotRid: request.spotRid,
        actorId: request.actorId
      })
      .packetName('JoinUserSpotActorReq')
      .timeout(5000)
      .submit<JoinUserSpotActorReply>(signal);
  }

  private async ensureRemoteActor(request: AuthReq, signal?: AbortSignal): Promise<EnsureActorReply> {
    return await this.route
      .request(SpotServiceNames.controlChannel, request.nodeRid, {
        actorId: request.actorId,
        displayName: request.displayName,
        nodeRid: request.nodeRid
      })
      .packetName('EnsureActorReq')
      .timeout(5000)
      .submit<EnsureActorReply>(signal);
  }

  private requireSingleBoundActor(packetName: string) {
    if (this.context.actors.bound.length === 1) {
      return this.context.actors.bound[0];
    }
    if (this.context.actors.bound.length === 0) {
      throw new Error(`No actor is bound for packet '${packetName}'.`);
    }
    throw new Error(`Actor id metadata is required for packet '${packetName}' with multiple bound actors.`);
  }
}

@Injectable()
export class ScenarioSessionFactory implements ZLinkSessionFactory<ScenarioSession> {
  constructor(
    @Inject(ZLINK_ROUTE_CLIENT) private readonly route: ZLinkRouteClient,
    @Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager,
    private readonly evidence: EvidenceStore
  ) {}

  create(context: ZLinkSessionContext): ScenarioSession {
    return new ScenarioSession(this.route, this.actors, this.evidence, context);
  }
}
