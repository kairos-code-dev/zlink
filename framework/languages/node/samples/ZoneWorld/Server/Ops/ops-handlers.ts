import { Inject, Injectable } from '@nestjs/common';
import { ZLINK_CHANNEL_CLIENT, ZLINK_FANOUT_CLIENT } from '@zlink-systems/nestjs';
import { ZLinkPacket } from '@zlink-systems/framework';
import {
  AnnounceWorldRes,
  ApplyNodeMaintenanceReq,
  NodeDiagnosticsRes,
  NodeMaintenanceChangedEvent,
  SetMaintenanceRes,
  WatchNodesRes,
  WorldAnnounceEvent
} from '../../Shared/contracts';
import { PacketNames } from '../../Shared/contracts';
import { ZoneWorldErrors, ZoneWorldNames } from '../../Shared/spec';
import { NodeRegistry } from './node-registry';
import type {
  AnnounceWorldReq,
  ApplyNodeMaintenanceRes,
  GetNodeDiagnosticsRes,
  NodeDiagnosticsReq,
  ReportNodeStatusMsg,
  SetMaintenanceReq
} from '../../Shared/contracts';
import type {
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkMessage,
  ZLinkSendContext,
  ZLinkSendHandler,
  ZLinkSessionContext,
  ZLinkSessionDispatchContext
} from '@zlink-systems/framework';

@Injectable()
class ReportNodeStatusHandler implements ZLinkSendHandler<ReportNodeStatusMsg> {
  constructor(private readonly nodes: NodeRegistry) {}

  async handle(message: ReportNodeStatusMsg, _context: ZLinkSendContext): Promise<void> {
    this.nodes.report(message);
  }
}

@Injectable()
@ZLinkPacket(PacketNames.watchNodesReq)
class WatchNodesHandler {
  constructor(private readonly nodes: NodeRegistry) {}

  async handle(context: ZLinkSessionContext): Promise<void> {
    context.client.reply(new WatchNodesRes(this.nodes.snapshot())).submit();
  }
}

@Injectable()
@ZLinkPacket(PacketNames.announceWorldReq)
class AnnounceWorldHandler {
  constructor(@Inject(ZLINK_FANOUT_CLIENT) private readonly fanout: ZLinkFanoutClient) {}

  async handle(
    context: ZLinkSessionContext,
    _dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage
  ): Promise<void> {
    const request = payload.decode<AnnounceWorldReq>(Object as never);
    const id = `announce-${Date.now()}-${Math.random().toString(16).slice(2)}`;
    await this.fanout
      .publish(ZoneWorldNames.broadcastChannel, ZoneWorldNames.announceTopic, new WorldAnnounceEvent(id, request.text))
      .submit();
    context.client.reply(new AnnounceWorldRes(id)).submit();
  }
}

@Injectable()
@ZLinkPacket(PacketNames.setMaintenanceReq)
class SetMaintenanceHandler {
  constructor(
    @Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient,
    @Inject(ZLINK_FANOUT_CLIENT) private readonly fanout: ZLinkFanoutClient
  ) {}

  async handle(
    context: ZLinkSessionContext,
    _dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage
  ): Promise<void> {
    const request = payload.decode<SetMaintenanceReq>(Object as never);
    try {
      const applied = await this.channels
        .requestToChannel(
          ZoneWorldNames.opsChannel(request.nodeId),
          new ApplyNodeMaintenanceReq(request.nodeId, request.enabled)
        )
        .timeout(3_000)
        .submit<ApplyNodeMaintenanceRes>();
      await this.fanout.publish(
        ZoneWorldNames.broadcastChannel,
        ZoneWorldNames.maintenanceTopic,
        new NodeMaintenanceChangedEvent(request.nodeId, request.enabled)
      ).submit();
      context.client.reply(new SetMaintenanceRes(applied.nodeId, applied.enabled, applied.zones)).submit();
    } catch {
      context.client.reply(new SetMaintenanceRes(request.nodeId, request.enabled, [], ZoneWorldErrors.nodeUnavailable)).submit();
    }
  }
}

@Injectable()
@ZLinkPacket(PacketNames.nodeDiagnosticsReq)
class NodeDiagnosticsHandler {
  constructor(@Inject(ZLINK_CHANNEL_CLIENT) private readonly channels: ZLinkChannelClient) {}

  async handle(
    context: ZLinkSessionContext,
    _dispatch: ZLinkSessionDispatchContext,
    payload: ZLinkMessage
  ): Promise<void> {
    const request = payload.decode<NodeDiagnosticsReq>(Object as never);
    try {
      const result = await this.channels
        .requestToChannel(ZoneWorldNames.opsChannel(request.nodeId), request)
        .timeout(3_000)
        .submit<GetNodeDiagnosticsRes>();
      context.client.reply(new NodeDiagnosticsRes(
        result.nodeId,
        result.zones,
        result.playerCount,
        result.maintenance
      )).submit();
    } catch {
      context.client.reply(new NodeDiagnosticsRes(request.nodeId, [], 0, false, ZoneWorldErrors.nodeUnavailable)).submit();
    }
  }
}

export {
  AnnounceWorldHandler,
  NodeDiagnosticsHandler,
  ReportNodeStatusHandler,
  SetMaintenanceHandler,
  WatchNodesHandler
};
