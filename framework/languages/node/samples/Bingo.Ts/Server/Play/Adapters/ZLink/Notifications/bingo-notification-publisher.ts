import { SampleNames } from '../../../../Configuration/sample-names';
import { Inject } from '@nestjs/common';
import { BingoNotificationDeliveryLog } from '../../../notification-delivery-log';
import type { BingoNotificationDeliveryLog as BingoNotificationDeliveryLogType } from '../../../notification-delivery-log';
import type {
  NumberDrawnNotify,
  PlayerJoinedNotify,
  StateEnvelope
} from '../../../../../Shared/Contracts/messages';

type BingoActorWithSession = {
  actorId: string;
};

type BingoNotificationEvent = {
  actor: BingoActorWithSession;
  packetName: string;
  payload: unknown;
};

class BingoNotificationPublisher {
  constructor(private readonly deliveryLog: BingoNotificationDeliveryLogType) {}

  async publish(events: BingoNotificationEvent[]): Promise<void> {
    for (const event of events) {
      this.deliveryLog.append(event.actor.actorId, event.packetName, event.payload);
    }
  }

  playerJoined(actor: BingoActorWithSession, payload: PlayerJoinedNotify): BingoNotificationEvent {
    return { actor, packetName: SampleNames.playerJoinedPacket, payload };
  }

  gameStarted(actor: BingoActorWithSession, payload: StateEnvelope): BingoNotificationEvent {
    return { actor, packetName: SampleNames.gameStartedPacket, payload };
  }

  numberDrawn(actor: BingoActorWithSession, payload: NumberDrawnNotify): BingoNotificationEvent {
    return { actor, packetName: SampleNames.numberDrawnPacket, payload };
  }

  gameEnded(actor: BingoActorWithSession, payload: StateEnvelope): BingoNotificationEvent {
    return { actor, packetName: SampleNames.gameEndedPacket, payload };
  }
}

Inject(BingoNotificationDeliveryLog)(BingoNotificationPublisher, undefined, 0);

export { BingoNotificationPublisher };
