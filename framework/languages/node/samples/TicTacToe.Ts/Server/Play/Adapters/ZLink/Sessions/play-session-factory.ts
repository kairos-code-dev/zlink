const { Inject } = require('@nestjs/common');
const { PlayActorFactory } = require('../Actors/play-actor-factory');
const { PlayEntrySpot } = require('../Spots/play-entry-spot');
const { PlayActorPlaceMarkHandler } = require('../Spots/Handlers/play-actor-place-mark-handler');
const { PlaySession } = require('./play-session');
const { loadSampleConfig } = require('../../../../Configuration/sample-config');

class PlaySessionFactory {
  [key: string]: any;
  constructor(actorFactory: any, entrySpot: any, placeMarkHandler: any) {
    this.actorFactory = actorFactory;
    this.entrySpot = entrySpot;
    this.placeMarkHandler = placeMarkHandler;
  }

  create(transport: any): any {
    return new PlaySession({
      apiEndpoint: loadSampleConfig().apiEndpoint,
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
