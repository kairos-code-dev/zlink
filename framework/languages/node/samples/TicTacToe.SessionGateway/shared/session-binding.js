class SessionBindingTable {
  constructor() {
    this.bindings = new Map();
    this.delivered = [];
  }

  bind(actorId, token) {
    this.bindings.set(actorId, token);
  }

  send(actorId, token, packetName, payload) {
    if (this.bindings.get(actorId) !== token) {
      return false;
    }
    this.delivered.push({ actorId, token, packetName, payload });
    return true;
  }
}

module.exports = { SessionBindingTable };
