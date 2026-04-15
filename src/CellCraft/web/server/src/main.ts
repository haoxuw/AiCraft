// CellCraft multiplayer server entry point.
// Usage: npm run dev  (or: PORT=7781 npm run dev)

import { WebSocketServer } from 'ws';
import { DEFAULT_MP_PORT } from '../../src/net/protocol.js';
import { GameServer } from './game_server.js';

const PORT = Number(process.env.PORT || DEFAULT_MP_PORT);

const game = new GameServer();
game.start();

const wss = new WebSocketServer({ port: PORT });
wss.on('connection', (ws) => {
  game.onConnect(ws);
});
wss.on('listening', () => {
  // eslint-disable-next-line no-console
  console.log(`[server] listening on ws://0.0.0.0:${PORT}`);
});

function shutdown(): void {
  // eslint-disable-next-line no-console
  console.log('[server] shutting down');
  game.stop();
  wss.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 500);
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
