import { Inject } from '@nestjs/common';
import { ZLINK_ROUTE_CLIENT, zlinkEntrySpotActorRequestHandler } from '@zlink-systems/nestjs';
import { GameplayActionService } from '../../Application/gameplay-action-service';
import { questMissionRouteRid, SampleNames } from '../../../../Shared/Configuration/sample-names';
import { PacketNames, getQuestProgressReq, syncQuestProgressReq } from '../../../../Shared/Contracts/messages';
import { GameQuestEntrySpot } from './gamequest-entry-spot';
import { GameQuestPlayerActor } from './gamequest-player-actor';
import type {
  CollectItemReq,
  CompleteMissionReq,
  EnterAreaReq,
  GetQuestProgressReq,
  GetQuestProgressRes,
  KillMonsterReq,
  SyncQuestProgressReq,
  SyncQuestProgressRes,
  UnlockFeatureReq
} from '../../../../Shared/Contracts/messages';
import type {
  ZLinkEntrySpotActorRequestHandler,
  ZLinkRouteClient,
  ZLinkSpotActorRequestContext
} from '@zlink-systems/framework';

abstract class GameQuestActionHandler<TRequest, TResponse> implements ZLinkEntrySpotActorRequestHandler<GameQuestEntrySpot, GameQuestPlayerActor, TRequest, TResponse> {
  constructor(protected readonly actions: GameplayActionService) {}
  abstract handle(spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TResponse>;
  protected requirePlayer(actor: GameQuestPlayerActor, playerId: string): void {
    if (actor.actorId !== playerId) throw new Error(`Actor '${actor.actorId}' cannot act for player '${playerId}'.`);
  }
}

@zlinkEntrySpotActorRequestHandler({ actor: () => GameQuestPlayerActor, entrySpot: () => GameQuestEntrySpot, packetName: PacketNames.killMonsterReq })
class KillMonsterHandler extends GameQuestActionHandler<KillMonsterReq, { eventId: string }> {
  constructor(actions: GameplayActionService) { super(actions); }
  async handle(_spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, _context: ZLinkSpotActorRequestContext, request: KillMonsterReq) {
    this.requirePlayer(actor, request.playerId);
    return (await this.actions.killMonster(request)).response;
  }
}

@zlinkEntrySpotActorRequestHandler({ actor: () => GameQuestPlayerActor, entrySpot: () => GameQuestEntrySpot, packetName: PacketNames.collectItemReq })
class CollectItemHandler extends GameQuestActionHandler<CollectItemReq, { eventId: string }> {
  constructor(actions: GameplayActionService) { super(actions); }
  async handle(_spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, _context: ZLinkSpotActorRequestContext, request: CollectItemReq) {
    this.requirePlayer(actor, request.playerId);
    return (await this.actions.collectItem(request)).response;
  }
}

@zlinkEntrySpotActorRequestHandler({ actor: () => GameQuestPlayerActor, entrySpot: () => GameQuestEntrySpot, packetName: PacketNames.completeMissionReq })
class CompleteMissionHandler extends GameQuestActionHandler<CompleteMissionReq, { eventId: string }> {
  constructor(actions: GameplayActionService) { super(actions); }
  async handle(_spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, _context: ZLinkSpotActorRequestContext, request: CompleteMissionReq) {
    this.requirePlayer(actor, request.playerId);
    return (await this.actions.completeMission(request)).response;
  }
}

@zlinkEntrySpotActorRequestHandler({ actor: () => GameQuestPlayerActor, entrySpot: () => GameQuestEntrySpot, packetName: PacketNames.enterAreaReq })
class EnterAreaHandler extends GameQuestActionHandler<EnterAreaReq, { eventId: string }> {
  constructor(actions: GameplayActionService) { super(actions); }
  async handle(_spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, _context: ZLinkSpotActorRequestContext, request: EnterAreaReq) {
    this.requirePlayer(actor, request.playerId);
    return (await this.actions.enterArea(request)).response;
  }
}

@zlinkEntrySpotActorRequestHandler({ actor: () => GameQuestPlayerActor, entrySpot: () => GameQuestEntrySpot, packetName: PacketNames.unlockFeatureReq })
class UnlockFeatureHandler extends GameQuestActionHandler<UnlockFeatureReq, { eventId: string }> {
  constructor(actions: GameplayActionService) { super(actions); }
  async handle(_spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, _context: ZLinkSpotActorRequestContext, request: UnlockFeatureReq) {
    this.requirePlayer(actor, request.playerId);
    return (await this.actions.unlockFeature(request)).response;
  }
}

abstract class QuestOwnerRequestHandler<TRequest extends { playerId: string }, TResponse>
  implements ZLinkEntrySpotActorRequestHandler<GameQuestEntrySpot, GameQuestPlayerActor, TRequest, TResponse> {
  constructor(@Inject(ZLINK_ROUTE_CLIENT) protected readonly routes: ZLinkRouteClient) {}
  protected requirePlayer(actor: GameQuestPlayerActor, playerId: string): void {
    if (actor.actorId !== playerId) throw new Error(`Actor '${actor.actorId}' cannot query player '${playerId}'.`);
  }
  protected request(playerId: string, request: object): Promise<TResponse> {
    return this.routes.requestToNode(SampleNames.questMissionRouteChannel, questMissionRouteRid(playerId), request)
      .timeout(SampleNames.requestTimeout).submit<TResponse>();
  }
  abstract handle(spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, context: ZLinkSpotActorRequestContext, request: TRequest): Promise<TResponse>;
}

@zlinkEntrySpotActorRequestHandler({ actor: () => GameQuestPlayerActor, entrySpot: () => GameQuestEntrySpot, packetName: PacketNames.getQuestProgressReq })
class GetQuestProgressHandler extends QuestOwnerRequestHandler<GetQuestProgressReq, GetQuestProgressRes> {
  constructor(@Inject(ZLINK_ROUTE_CLIENT) routes: ZLinkRouteClient) { super(routes); }
  async handle(_spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, _context: ZLinkSpotActorRequestContext, request: GetQuestProgressReq) {
    this.requirePlayer(actor, request.playerId);
    return await this.request(request.playerId, getQuestProgressReq(request.playerId));
  }
}

@zlinkEntrySpotActorRequestHandler({ actor: () => GameQuestPlayerActor, entrySpot: () => GameQuestEntrySpot, packetName: PacketNames.syncQuestProgressReq })
class SyncQuestProgressHandler extends QuestOwnerRequestHandler<SyncQuestProgressReq, SyncQuestProgressRes> {
  constructor(@Inject(ZLINK_ROUTE_CLIENT) routes: ZLinkRouteClient) { super(routes); }
  async handle(_spot: GameQuestEntrySpot, actor: GameQuestPlayerActor, _context: ZLinkSpotActorRequestContext, request: SyncQuestProgressReq) {
    this.requirePlayer(actor, request.playerId);
    return await this.request(request.playerId, syncQuestProgressReq(request.playerId));
  }
}

export {
  KillMonsterHandler,
  CollectItemHandler,
  CompleteMissionHandler,
  EnterAreaHandler,
  UnlockFeatureHandler,
  GetQuestProgressHandler,
  SyncQuestProgressHandler
};
