// SPDX-License-Identifier: MPL-2.0

const normalize = value => value.toLowerCase()
  .replace(/[^a-z0-9]+/gu, '');

export const semanticMemberKey = member => [
  member.language,
  normalize(member.ownerIdentity),
  normalize(member.memberName),
  member.kind,
].join('\0');

const runtimeLanguage = language => language === 'kotlin' ? 'java' : language;

const rules = [
  {
    id: 'redis-transfer-store-split',
    matches: value => /redis.*checkpoint|checkpoint.*redis/u.test(value),
    decisions: ['CA-D31', 'CA-D34', 'CA-D36'],
    coverage: () => ['e2e:add:redis-stores-shared-deployment'],
  },
  {
    id: 'transfer-store-vocabulary',
    matches: value => /checkpoint|transferstore|transferreference|transferstored/u.test(value)
      && !/redis/u.test(value),
    decisions: ['CA-D31', 'CA-D36'],
    coverage: () => ['e2e:add:transfer-store-required-registration'],
  },
  {
    id: 'instance-spot-explicit-create',
    matches: value => /instancespotaddress|requesttospot|sendtospot/u.test(value),
    decisions: ['CA-D25'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'automatic-routing-id',
    matches: value => /routingidslot|allocatedroutingid|routingidallocation|useallocatedroutingid|setroutingidallocation/u.test(value),
    decisions: ['CA-D20', 'CA-D21'],
    coverage: () => ['e2e:add:automatic-rid-collision'],
  },
  {
    id: 'exact-session-bind',
    matches: value => /sessionactors.*bind|bindasync|bindactor|enableactordispatch/u.test(value),
    decisions: ['CA-D05', 'CA-D06'],
    coverage: () => ['e2e:add:exact-generation-mutation-bind'],
  },
  {
    id: 'exact-object-mutation',
    matches: value => /destroy|closespot|closeasync/u.test(value),
    decisions: ['CA-D27'],
    coverage: () => ['e2e:add:exact-generation-mutation-bind'],
  },
  {
    id: 'actor-spot-relocation',
    matches: value => /joinentryspot/u.test(value),
    decisions: ['CA-D07'],
    coverage: () => ['e2e:add:same-node-join-without-transfer-payload'],
  },
  {
    id: 'actor-manager-replacement',
    matches: value => /actordirectory|actorspothandleresolver|sessionactormanager|sessionactor(?:t|\b)|actorref|findactor/u.test(value)
      && !/iszlinkframeworkerrorretriablebydefault/u.test(value),
    decisions: ['CA-D01', 'CA-D26'],
    coverage: () => ['e2e:add:global-actor-remote-create'],
  },
  {
    id: 'spot-manager-replacement',
    matches: value => /spothandleresolver|spothandle|spotmanager.*(?:find|list)|spotinfo/u.test(value)
      && !/actorspothandleresolver/u.test(value),
    decisions: ['CA-D02', 'CA-D26'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'global-authority-key',
    matches: value => /actorlocationkey|spotlocationkey|authoritysnapshot/u.test(value),
    decisions: ['CA-D01', 'CA-D02'],
    coverage: member => [`regression:add:global-authority-key:${runtimeLanguage(member.language)}`],
  },
  {
    id: 'placement-policy',
    matches: value => /placement|affinity|capacity|weight|spottypes|objectcapability/u.test(value)
      && !/zlinksocket/u.test(value),
    decisions: ['CA-D14', 'CA-D22', 'CA-D23'],
    coverage: () => ['e2e:add:placement-capacity-weight-affinity'],
  },
  {
    id: 'reservation-recovery',
    matches: value => /reservation|reserve|commitreservation|abortreservation/u.test(value),
    decisions: ['CA-D24'],
    coverage: () => ['e2e:add:reservation-crash-recovery'],
  },
  {
    id: 'forwarding-and-cache',
    matches: value => /forwardwindow|forwarding|routecache|route.*cache/u.test(value),
    decisions: ['CA-D16', 'CA-D17'],
    coverage: () => ['e2e:add:forwarding-bounds'],
  },
  {
    id: 'actor-fluent-create',
    matches: value => /actorfactory|actormanager.*create|actormanager.*getorcreate|requesttoactor|sendtoactor/u.test(value),
    decisions: ['CA-D01', 'CA-D04', 'CA-D09'],
    coverage: () => ['e2e:add:global-actor-remote-create'],
  },
  {
    id: 'spot-fluent-create',
    matches: (value, member) => /spotfactory|spotmanager.*create|spotmanager.*getorcreate|spotcreate/u.test(value)
      && !/meshnodebuilder|nestmeshnodebuilder/u.test(normalize(member.ownerIdentity)),
    decisions: ['CA-D02', 'CA-D04', 'CA-D09'],
    coverage: () => ['e2e:add:global-spot-explicit-create'],
  },
  {
    id: 'closed-object-role',
    matches: value => /addentryspot|configureentryspot|entryspotoptions|meshnodebuilder.*add.*spot/u.test(value),
    decisions: ['CA-D18', 'CA-D28'],
    coverage: () => ['sample:add:remote-object-create'],
  },
  {
    id: 'operational-location-query',
    matches: value => /peerlocationresolver|listlivepeers|locationextensions|locationruntimequery|list.*location|listtopology|listmeshnode/u.test(value)
      && !/resolve.*handle/u.test(value),
    decisions: ['CA-D26'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'fixed-entry-routing-id',
    matches: value => /setentryspotroutingid/u.test(value),
    decisions: ['CA-D19'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'kotlin-reviewed-contract-set',
    matches: (value, member) => member.language === 'kotlin'
      && /zlinkframeworkextensionskt|zlinksuspending|zlinkdispatchoptionsextensionskt|package/u.test(value)
      && !/actorfactory|actorref|findactor|listlivepeers|list.*location|listtopology|listmeshnode|resolve.*handle/u.test(value),
    decisions: ['CA-D29'],
    coverage: () => ['public-behavior:formal-contract-parity:kotlin'],
  },
  {
    id: 'node-reviewed-contract-set',
    matches: (value, member) => member.language === 'node'
      && /zlinksocket|zlinkspotevent|zlinkspotlocationfilter|zlinkactorlocationfilter|zlinkdecoratormetadata|zlinkframeworkerrorkindvalues|zlinkspotactorrequest|zlinkspotactorreplyoptions|zlinkspotpeer|zlinksession|zlinkactorjoinresult|iszlinkframeworkerrorretriablebydefault/i.test(value),
    decisions: ['CA-D29'],
    coverage: () => ['public-behavior:formal-contract-parity:node'],
  },
  {
    id: 'generated-record-contract-set',
    matches: value => /zlinkmeshNodeDescriptorhashset/i.test(value),
    decisions: ['CA-D29'],
    coverage: member => [`public-behavior:formal-contract-parity:${member.language}`],
  },
  {
    id: 'redis-location-options-split',
    matches: value => /mutable.*redislocationoptions/u.test(value),
    decisions: ['CA-D34'],
    coverage: () => ['e2e:add:redis-stores-shared-deployment'],
  },
];

export function auditRemovedMemberBehavior(member) {
  const value = normalize(`${member.ownerIdentity}.${member.memberName}`);
  const matches = rules.filter(candidate => candidate.matches(value, member));
  if (matches.length !== 1) {
    return {
      state: matches.length === 0 ? 'unmatched' : 'ambiguous',
      ruleIds: matches.map(rule => rule.id),
    };
  }
  const [rule] = matches;
  return {state: 'matched', behavior: {
    ruleId: rule.id,
    decisionCoverage: rule.decisions,
    replacementCoverage: rule.coverage(member),
  }};
}

export function removedMemberBehavior(member) {
  const audit = auditRemovedMemberBehavior(member);
  if (audit.state !== 'matched') {
    throw new Error(`${audit.state}:${audit.ruleIds.join(',')} removed public member: ${member.identity}`);
  }
  return audit.behavior;
}
