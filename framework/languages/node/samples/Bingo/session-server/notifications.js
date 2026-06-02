class BoundNotificationHub {
  constructor() {
    this.messages = [];
  }

  sessionFor(actorId) {
    return {
      send: (payload) => {
        let packetName;
        return {
          metadata() { return this; },
          packetName(name) {
            packetName = name;
            return this;
          },
          submit: async () => {
            this.messages.push({ actorId, packetName, payload });
          }
        };
      },
      disconnect: async () => {}
    };
  }
}

module.exports = { BoundNotificationHub };
