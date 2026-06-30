import { PacketNames } from '../../Shared/Contracts/messages';
import type { ZLinkSessionContext } from '@zlink-systems/framework';
import type { DeliveryStatusNotify } from '../../Shared/Contracts/messages';

class CustomerSessionDirectory {
  private readonly sessions = new Map<string, ZLinkSessionContext>();
  private readonly subscriptions = new Map<string, Set<string>>();

  add(context: ZLinkSessionContext): void {
    this.sessions.set(context.sessionId, context);
  }

  remove(context: ZLinkSessionContext): void {
    this.sessions.delete(context.sessionId);
    for (const sessions of this.subscriptions.values()) {
      sessions.delete(context.sessionId);
    }
  }

  subscribe(context: ZLinkSessionContext, deliveryId: string): void {
    this.sessions.set(context.sessionId, context);
    let sessions = this.subscriptions.get(deliveryId);
    if (sessions === undefined) {
      sessions = new Set();
      this.subscriptions.set(deliveryId, sessions);
    }
    sessions.add(context.sessionId);
  }

  async publish(status: DeliveryStatusNotify): Promise<void> {
    const sessionIds = this.subscriptions.get(status.deliveryId);
    if (sessionIds === undefined) {
      return;
    }
    for (const sessionId of sessionIds) {
      const session = this.sessions.get(sessionId);
      if (session !== undefined) {
        await session.client.send(status).packetName(PacketNames.deliveryStatusNotify).submit();
      }
    }
  }
}

export { CustomerSessionDirectory };
