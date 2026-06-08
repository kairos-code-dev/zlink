# Bingo

Two-client Session/API/Play/Registry sample check.

The client opens only the Session stream endpoint. Each player authenticates,
requests matching, receives game-start push after the second join, submits a
3 x 3 card, and waits for server timer draw and game-ended notifications. The
client never sends draw requests.
