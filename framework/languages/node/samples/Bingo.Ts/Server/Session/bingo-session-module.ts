const { Module } = require('@nestjs/common');
const { ZLinkModule, zlinkFramework } = require('../../../../../packages/nestjs/dist');
const { SessionAuthenticator } = require('./Sessions/Handlers/authenticate-session-handler');
const { SampleNames } = require('../Configuration/sample-names');

function createBingoSessionModule(endpoints: {
  apiEndpoint: string;
  playEndpoint: string;
}) {
  class BingoSessionModule {}

  Module({
    imports: [
      ZLinkModule.forRootFactory({
        useFactory: () => zlinkFramework()
          .clientServerChannel(SampleNames.apiChannel, (channel) => channel
            .client(endpoints.apiEndpoint))
          .clientServerChannel(SampleNames.playChannel, (channel) => channel
            .client(endpoints.playEndpoint))
          .build()
      })
    ],
    providers: [
      SessionAuthenticator
    ]
  })(BingoSessionModule);

  return BingoSessionModule;
}

function getSessionAuthenticator(app: { get(token: unknown, options?: { strict?: boolean }): unknown }): InstanceType<typeof SessionAuthenticator> {
  return app.get(SessionAuthenticator, { strict: false }) as InstanceType<typeof SessionAuthenticator>;
}

export { createBingoSessionModule, getSessionAuthenticator };
