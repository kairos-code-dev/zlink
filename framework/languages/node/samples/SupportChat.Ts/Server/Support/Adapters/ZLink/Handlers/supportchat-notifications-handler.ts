import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { SupportNotificationDeliveryLog } from '../../../notification-delivery-log';
import { PacketNames } from '../../../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { SupportNotificationDeliveryLog as SupportNotificationDeliveryLogType } from '../../../notification-delivery-log';
import type {
  SupportNotificationBatch,
  SupportNotificationsReq,
  UserIdentity
} from '../../../../../Shared/Contracts/messages';

// The notifications channel long-polls the bound actor's pending push frames. The Session
// server pumps the returned frames to the actor's stream session as server push messages.
@zlinkRequestHandler('notifications', PacketNames.supportNotificationsReq)
class SupportChatNotificationsHandler implements ZLinkRequestHandler<SupportNotificationsReq & UserIdentity, SupportNotificationBatch> {
  constructor(@Inject(SupportNotificationDeliveryLog) private readonly deliveryLog: SupportNotificationDeliveryLogType) {}

  async handle(request: SupportNotificationsReq & UserIdentity): Promise<SupportNotificationBatch> {
    const batch = await this.deliveryLog.waitFor(request.actorId, request.afterSeq);
    return {
      nextSeq: batch.nextSeq,
      delivered: batch.delivered.map((entry) => ({
        seq: entry.seq,
        actorId: entry.actorId,
        packetName: entry.packetName,
        payload: entry.payload
      }))
    };
  }
}

export { SupportChatNotificationsHandler };
