# Java Handler Interface Document Location

Java handler and application-facing public signatures are provided per
feature in the
[exact interface table of contents](interfaces/README.ko.md).

- [Channel Messaging](interfaces/channel-messaging.ko.md)
- [Spot](interfaces/spots.ko.md)
- [Actor](interfaces/actors.ko.md)
- [STREAM Session](interfaces/stream-session.en.md)
- [Monitoring](interfaces/monitoring.ko.md)

Actor and Instance Spot relocation behavior is wired directly into the
factory configure callback, and a separate relocation registry isn't
kept.
