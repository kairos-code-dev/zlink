import { Inject } from '@nestjs/common';
import { zlinkRequestHandler } from '@zlink-systems/nestjs';
import { BingoNotificationDeliveryLog } from '../../../notification-delivery-log';
import { bingoPayloadBase64 } from '../../../../../Shared/Contracts/protobuf-codec';
import { PacketNames, bingoNotificationBatch } from '../../../../../Shared/Contracts/messages';
import type { ZLinkRequestHandler } from '@zlink-systems/framework';
import type { BingoNotificationDeliveryLog as BingoNotificationDeliveryLogType } from '../../../notification-delivery-log';
import type {
  BingoNotificationsReq,
  PlayerIdentity
} from '../../../../../Shared/Contracts/messages';

@zlinkRequestHandler('notifications', PacketNames.bingoNotificationsReq)
class BingoNotificationsHandler implements ZLinkRequestHandler<BingoNotificationsReq & PlayerIdentity, unknown> {
  constructor(@Inject(BingoNotificationDeliveryLog) private readonly boundSessions: BingoNotificationDeliveryLogType) {}

  async handle(request: BingoNotificationsReq & PlayerIdentity): Promise<unknown> {
    const batch = await this.boundSessions.waitFor(request.actorId, request.afterSeq);
    return bingoNotificationBatch({
      nextSeq: batch.nextSeq,
      delivered: batch.delivered.map((entry) => ({
        seq: entry.seq,
        actorId: entry.actorId,
        packetName: entry.packetName,
        payloadBase64: bingoPayloadBase64(entry.payload, entry.packetName)
      }))
    });
  }
}

export { BingoNotificationsHandler };
