const { SampleNames } = require('../../../Shared/Configuration/sample-names');
import type {
  NumberDrawnNotify,
  PlayerJoinedNotify,
  StateEnvelope
} from '../../../Shared/Contracts/messages';

type BingoActorWithSession = {
  boundSession: {
    send(payload: unknown): {
      packetName(packetName: string): {
        submit(): Promise<void>;
      };
    };
  };
};

type BingoNotificationEvent = {
  actor: BingoActorWithSession;
  packetName: string;
  payload: unknown;
};

class BingoNotificationPublisher {
  [key: string]: any;
  async publish(events: BingoNotificationEvent[]): Promise<void> {
    for (const event of events) {
      await event.actor.boundSession
        .send(event.payload)
        .packetName(event.packetName)
        .submit();
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

export { BingoNotificationPublisher };
