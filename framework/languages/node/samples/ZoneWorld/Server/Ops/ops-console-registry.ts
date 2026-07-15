import { Injectable } from '@nestjs/common';
import { NodeStatusNotify } from '../../Shared/contracts';
import type { NodeView } from '../../Shared/contracts';
import type { ZLinkSessionContext } from '@zlink-systems/framework';

@Injectable()
class OpsConsoleRegistry {
  private readonly consoles = new Map<string, ZLinkSessionContext>();

  add(context: ZLinkSessionContext): void {
    this.consoles.set(context.sessionId, context);
  }

  remove(context: ZLinkSessionContext): void {
    this.consoles.delete(context.sessionId);
  }

  publish(node: NodeView): void {
    const notify = new NodeStatusNotify(
      node.nodeId,
      node.registered,
      node.connected,
      node.maintenance,
      node.zones,
      node.playerCount
    );
    for (const context of this.consoles.values()) context.client.send(notify).submit();
  }
}

export { OpsConsoleRegistry };
