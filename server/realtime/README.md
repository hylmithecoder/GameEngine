# ilmeee-realtime

Realtime collaboration server for the Ilmeee editor. Rooms are ordered
last-writer-wins key/value stores; the server sequences writes, echoes them to
the whole room and replays the current values to late joiners. It does not
understand scenes — the editor does (`include/core_engine/CollabCodec.hpp`).

- `GET /health` — `{"status":"ok", rooms, peers, version, protocol, uptime_secs}`
- `GET /ws` — WebSocket, engine envelope protocol 1

```bash
make run     # ws://127.0.0.1:9230/ws, token "dev", rooms in ./data
make lint test
make build TARGET=x86_64-unknown-linux-musl   # static binary for the tunnel server
```

Configuration, deployment and the message table: [docs/RealtimeCollab.md](../../docs/RealtimeCollab.md).
