const { SampleNames } = require('../../../Shared/Configuration/sample-names');

class BingoNotificationPublisher {
  async publish(events) {
    for (const event of events) {
      await event.actor.boundSession
        .send(event.payload)
        .packetName(event.packetName)
        .submit();
    }
  }

  playerJoined(actor, payload) {
    return { actor, packetName: SampleNames.playerJoinedPacket, payload };
  }

  gameStarted(actor, payload) {
    return { actor, packetName: SampleNames.gameStartedPacket, payload };
  }

  numberDrawn(actor, payload) {
    return { actor, packetName: SampleNames.numberDrawnPacket, payload };
  }

  gameEnded(actor, payload) {
    return { actor, packetName: SampleNames.gameEndedPacket, payload };
  }
}

module.exports = { BingoNotificationPublisher };
