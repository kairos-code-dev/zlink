const framework = require('../../../../packages/framework/dist');
const { Inject } = require('@nestjs/common');
const { SampleBoundSessionRuntime } = require('../../../shared/bound-session-runtime');
const { SessionPlayerActorFactory } = require('../../Shared/Actors/player-actor');
const { SampleNames } = require('../../Shared/Configuration/sample-names');

class SessionGatewayActorManager extends framework.DefaultZLinkActorManager {
  constructor(boundSessions) {
    super({
      actorFactories: new Map([[SampleNames.playerActorType, SessionPlayerActorFactory]]),
      boundSessionFactory(actorId) {
        return boundSessions.createBoundSession(actorId);
      }
    });
  }
}

Inject(SampleBoundSessionRuntime)(SessionGatewayActorManager, undefined, 0);

module.exports = { SessionGatewayActorManager };
