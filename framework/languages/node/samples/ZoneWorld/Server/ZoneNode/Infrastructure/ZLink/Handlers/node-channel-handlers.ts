import { Inject, Injectable } from '@nestjs/common';
import {
  ZLINK_ACTOR_MANAGER,
  ZLINK_SPOT_HANDLE_RESOLVER,
  ZLINK_SPOT_OUTBOUND
} from '@zlink-systems/nestjs';
import { ZONEWORLD_CONFIG } from '../../../../Configuration/configuration';
import type { ZoneWorldConfiguration } from '../../../../Configuration/configuration';
import { ZoneWorldNames, zonesOf } from '../../../../../Shared/spec';
import { DeliverAnnounceMsg, EnsurePlayerActorRes } from '../../../../../Shared/contracts';
import type {
  ApplyNodeMaintenanceReq,
  ApplyNodeMaintenanceRes,
  EnsurePlayerActorReq,
  GetNodeDiagnosticsReq,
  GetNodeDiagnosticsRes
} from '../../../../../Shared/contracts';
import type {
  ZLinkActorManager,
  ZLinkPublishContext,
  ZLinkPublishHandler,
  ZLinkRequestContext,
  ZLinkRequestHandler,
  ZLinkSpotHandleResolver,
  ZLinkSpotOutbound
} from '@zlink-systems/framework';
import type { NodeMaintenanceChangedEvent, WorldAnnounceEvent } from '../../../../../Shared/contracts';

@Injectable()
class ApplyNodeMaintenanceHandler implements
  ZLinkRequestHandler<ApplyNodeMaintenanceReq, ApplyNodeMaintenanceRes> {
  constructor(@Inject(ZONEWORLD_CONFIG) private readonly config: ZoneWorldConfiguration) {}

  async handle(request: ApplyNodeMaintenanceReq, _context: ZLinkRequestContext): Promise<ApplyNodeMaintenanceRes> {
    const nodeId = this.nodeId();
    if (request.nodeId !== nodeId) throw new Error(`Maintenance request targets '${request.nodeId}', not '${nodeId}'.`);
    return { nodeId, enabled: request.enabled, zones: [...zonesOf(nodeId)] };
  }

  private nodeId(): string {
    if (this.config.zoneNode === undefined) throw new Error('ZoneNode configuration is required.');
    return this.config.zoneNode.nodeId;
  }
}

@Injectable()
class GetNodeDiagnosticsHandler implements
  ZLinkRequestHandler<GetNodeDiagnosticsReq, GetNodeDiagnosticsRes> {
  constructor(@Inject(ZONEWORLD_CONFIG) private readonly config: ZoneWorldConfiguration) {}

  async handle(request: GetNodeDiagnosticsReq, _context: ZLinkRequestContext): Promise<GetNodeDiagnosticsRes> {
    const nodeId = this.config.zoneNode?.nodeId;
    if (nodeId === undefined || request.nodeId !== nodeId) throw new Error('Diagnostics request targets another node.');
    return { nodeId, zones: [...zonesOf(nodeId)], playerCount: 0, maintenance: false };
  }
}

@Injectable()
class EnsurePlayerActorHandler implements ZLinkRequestHandler<EnsurePlayerActorReq, EnsurePlayerActorRes> {
  constructor(@Inject(ZLINK_ACTOR_MANAGER) private readonly actors: ZLinkActorManager) {}

  async handle(request: EnsurePlayerActorReq, _context: ZLinkRequestContext): Promise<EnsurePlayerActorRes> {
    if (!/^[a-z0-9-]{1,32}$/.test(request.playerId)) throw new Error(`Invalid player id '${request.playerId}'.`);
    const actor = await this.actors.getOrCreate(request.playerId, ZoneWorldNames.playerActorType, request);
    return new EnsurePlayerActorRes(request.playerId, {
      nodeRid: String(actor.nodeRid),
      actorId: actor.actorId,
      generation: actor.generation.toString()
    });
  }
}

@Injectable()
class WorldAnnounceSubscriber implements ZLinkPublishHandler<WorldAnnounceEvent> {
  constructor(
    @Inject(ZONEWORLD_CONFIG) private readonly config: ZoneWorldConfiguration,
    @Inject(ZLINK_SPOT_HANDLE_RESOLVER) private readonly handles: ZLinkSpotHandleResolver,
    @Inject(ZLINK_SPOT_OUTBOUND) private readonly outbound: ZLinkSpotOutbound
  ) {}

  async handle(message: WorldAnnounceEvent, context: ZLinkPublishContext): Promise<void> {
    console.log(`fanout subscriber received announcement id=${message.announcementId} topic=${context.topic}`);
    const nodeId = this.config.zoneNode?.nodeId;
    if (nodeId === undefined) return;
    for (const zoneId of zonesOf(nodeId)) {
      const handle = await this.handles.resolveSpotHandle(zoneId);
      if (handle !== undefined) {
        this.outbound.sendToSpot(handle, new DeliverAnnounceMsg(message.announcementId, message.text)).submit();
      }
    }
  }
}

@Injectable()
class MaintenanceChangedSubscriber implements ZLinkPublishHandler<NodeMaintenanceChangedEvent> {
  async handle(message: NodeMaintenanceChangedEvent, context: ZLinkPublishContext): Promise<void> {
    console.log(`maintenance cache updated node=${message.nodeId} enabled=${message.enabled} topic=${context.topic}`);
  }
}

export {
  ApplyNodeMaintenanceHandler,
  EnsurePlayerActorHandler,
  GetNodeDiagnosticsHandler,
  MaintenanceChangedSubscriber,
  WorldAnnounceSubscriber
};
