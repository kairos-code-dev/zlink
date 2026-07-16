import { Injectable } from '@nestjs/common';
import type { NodeView, ReportNodeStatusMsg } from '../../Shared/contracts';

type NodeState = { view: NodeView; transportConnected: boolean };

@Injectable()
class NodeRegistry {
  private readonly nodes = new Map<string, NodeState>();
  private readonly nodeByRoutingId = new Map<string, string>();
  private readonly routingIdByNode = new Map<string, string>();
  private liveRoutingIds = new Set<string>();

  report(message: ReportNodeStatusMsg): NodeView {
    const oldNodeId = this.nodeByRoutingId.get(message.nodeRid);
    if (oldNodeId !== undefined && oldNodeId !== message.nodeId) this.routingIdByNode.delete(oldNodeId);
    const oldRoutingId = this.routingIdByNode.get(message.nodeId);
    if (oldRoutingId !== undefined && oldRoutingId !== message.nodeRid) this.nodeByRoutingId.delete(oldRoutingId);
    this.nodeByRoutingId.set(message.nodeRid, message.nodeId);
    this.routingIdByNode.set(message.nodeId, message.nodeRid);
    return this.update(message.nodeId, () => ({
      transportConnected: true,
      view: {
        nodeId: message.nodeId,
        registered: this.liveRoutingIds.has(message.nodeRid),
        connected: this.liveRoutingIds.has(message.nodeRid),
        maintenance: message.maintenance,
        zones: [...message.zones],
        playerCount: message.playerCount
      }
    }));
  }

  applyLiveRoutingIds(routingIds: ReadonlySet<string>): NodeView[] {
    this.liveRoutingIds = new Set(routingIds);
    return [...this.routingIdByNode.entries()].map(([nodeId, routingId]) =>
      this.update(nodeId, (state) => ({
        ...state,
        view: {
          ...state.view,
          registered: routingIds.has(routingId),
          connected: routingIds.has(routingId) && state.transportConnected
        }
      }))
    );
  }

  applyConnection(routingId: string, connected: boolean): NodeView | undefined {
    const nodeId = this.nodeByRoutingId.get(routingId);
    if (nodeId === undefined) return undefined;
    return this.update(nodeId, (state) => ({
      transportConnected: connected,
      view: { ...state.view, connected: state.view.registered && connected }
    }));
  }

  snapshot(): NodeView[] {
    return [...this.nodes.values()]
      .map((state) => state.view)
      .sort((left, right) => Buffer.from(left.nodeId).compare(Buffer.from(right.nodeId)));
  }

  private update(nodeId: string, change: (state: NodeState) => NodeState): NodeView {
    const current = this.nodes.get(nodeId) ?? {
      transportConnected: false,
      view: {
        nodeId,
        registered: false,
        connected: false,
        maintenance: false,
        zones: [],
        playerCount: 0
      }
    };
    const updated = change(current);
    this.nodes.set(nodeId, updated);
    return updated.view;
  }
}

export { NodeRegistry };
