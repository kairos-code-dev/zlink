const framework = require('../../../packages/framework/dist');
const { SessionBindingTable } = require('../shared/session-binding');
const { SessionPlayerActorFactory } = require('../play-server/session-actor');

function createSessionGateway() {
  const bindings = new SessionBindingTable();
  let currentToken = 'session-1';
  const manager = new framework.DefaultZLinkActorManager({
    actorFactories: new Map([['player', SessionPlayerActorFactory]]),
    boundSessionFactory(actorId) {
      return {
        send(message) {
          let packetName;
          return {
            metadata() { return this; },
            packetName(name) {
              packetName = name;
              return this;
            },
            async submit() {
              if (!bindings.send(actorId, currentToken, packetName, message)) {
                throw new Error('stale session binding');
              }
            }
          };
        },
        async disconnect() {
          bindings.bind(actorId, undefined);
        }
      };
    }
  });

  return {
    bindings,
    manager,
    bind(actorId, token) {
      currentToken = token;
      bindings.bind(actorId, token);
    },
    staleSend(actorId, token) {
      return bindings.send(actorId, token, 'StalePush', { stale: true });
    }
  };
}

module.exports = { createSessionGateway };
