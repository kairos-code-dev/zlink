import { Injectable } from '@nestjs/common';
import type { NodeView, ReportNodeStatusMsg } from '../../Shared/contracts';

@Injectable()
class NodeRegistry {
  private readonly nodes = new Map<string, NodeView>();

  report(message: ReportNodeStatusMsg): NodeView {
    const current = this.nodes.get(message.nodeId);
    const updated: NodeView = {
      nodeId: message.nodeId,
      registered: current?.registered ?? true,
      connected: true,
      maintenance: message.maintenance,
      zones: [...message.zones],
      playerCount: message.playerCount
    };
    this.nodes.set(message.nodeId, updated);
    return updated;
  }

  snapshot(): NodeView[] {
    return [...this.nodes.values()].sort((left, right) => Buffer.from(left.nodeId).compare(Buffer.from(right.nodeId)));
  }
}

export { NodeRegistry };
