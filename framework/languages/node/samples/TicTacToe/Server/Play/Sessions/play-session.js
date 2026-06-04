const { createChannelClient } = require('../../../../shared/channel-runtime');
const { PacketNames, SampleNames, SampleTimings } = require('../../../Shared/Contracts/messages');

class PlaySession {
  constructor(dependencies, transport) {
    this.dependencies = dependencies;
    this.transport = transport;
    this.actor = null;
  }

  async dispatch(header, payload) {
    if (header.name === PacketNames.authenticateReq) {
      await this.authenticate(header, payload);
      return;
    }
    if (this.actor === null) {
      throw new Error('AuthenticateReq is required before play packets.');
    }
    if (header.name === PacketNames.joinGameReq) {
      await this.joinGame(header, payload);
      return;
    }
    if (header.name === PacketNames.placeMarkReq) {
      await this.placeMark(header, payload);
      return;
    }
    throw new Error(`Unsupported play stream packet '${header.name}'.`);
  }

  async authenticate(header, request) {
    const apiClient = await createChannelClient({
      channelName: SampleNames.apiChannel,
      peers: [this.dependencies.apiEndpoint]
    });
    try {
      const authenticated = await apiClient
        .requestToChannel(SampleNames.apiChannel, { accessToken: request.accessToken })
        .packetName(PacketNames.authenticatePlayerReq)
        .timeout(SampleTimings.requestTimeout)
        .submit();
      this.actor = this.dependencies.actorFactory.ensure(authenticated.actorId);
      this.actor.displayName = authenticated.displayName;
      this.actor.session = this;
      this.transport.reply(header, {
        actorId: authenticated.actorId,
        displayName: authenticated.displayName
      });
    } finally {
      await apiClient.stop();
    }
  }

  async joinGame(header, request) {
    const result = this.dependencies.entrySpot.join(this.actor, request.gameId);
    this.transport.reply(header, result);
    await this.flushAllNotifications();
  }

  async placeMark(header, request) {
    const result = this.dependencies.placeMarkHandler.handle({
      actor: this.actor,
      gameId: request.gameId,
      cell: request.cell
    });
    this.transport.reply(header, result);
    await this.flushAllNotifications();
  }

  async flushAllNotifications() {
    for (const actor of this.dependencies.actorFactory.actors.values()) {
      if (actor.session !== null) {
        actor.session.flushNotifications(actor);
      }
    }
  }

  flushNotifications(actor) {
    while (actor.notifications.length > 0) {
      const notification = actor.notifications.shift();
      this.transport.send(notification.packetName, notification.payload, { seq: String(notification.seq) });
    }
  }
}

module.exports = { PlaySession };
