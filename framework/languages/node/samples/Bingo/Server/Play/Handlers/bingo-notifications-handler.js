const { Inject } = require('@nestjs/common');
const { SampleBoundSessionRuntime } = require('../../../../shared/bound-session-runtime');

class BingoNotificationsHandler {
  constructor(boundSessions) {
    this.boundSessions = boundSessions;
  }

  async handle(request) {
    return await this.boundSessions.deliveredFor(request.actorId, request.afterSeq);
  }
}

Inject(SampleBoundSessionRuntime)(BingoNotificationsHandler, undefined, 0);

module.exports = { BingoNotificationsHandler };
