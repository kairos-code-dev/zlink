const { Inject } = require('@nestjs/common');
const { PlayActorFactory } = require('../Actors/play-actor-factory');
const { PlayEntrySpot } = require('../EntrySpot/play-entry-spot');
const { PlayActorPlaceMarkHandler } = require('../GameSpots/Handlers/play-actor-place-mark-handler');
const { PlaySession } = require('./play-session');

class PlaySessionFactory {
  [key: string]: any;
  constructor(actorFactory, entrySpot, placeMarkHandler) {
    this.actorFactory = actorFactory;
    this.entrySpot = entrySpot;
    this.placeMarkHandler = placeMarkHandler;
  }

  create(transport) {
    return new PlaySession({
      apiEndpoint: process.env.TICTACTOE_API_ENDPOINT,
      actorFactory: this.actorFactory,
      entrySpot: this.entrySpot,
      placeMarkHandler: this.placeMarkHandler
    }, transport);
  }
}

Inject(PlayActorFactory)(PlaySessionFactory, undefined, 0);
Inject(PlayEntrySpot)(PlaySessionFactory, undefined, 1);
Inject(PlayActorPlaceMarkHandler)(PlaySessionFactory, undefined, 2);

export { PlaySessionFactory };
