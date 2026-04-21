import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import https from 'node:https';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import { OllamaVisionAdapter } from './adapters/ollama-vision.js';
import { LlamaCppVisionAdapter } from './adapters/llamacpp-vision.js';
import { FallbackAdapter } from './adapters/fallback-adapter.js';
import { getLiteRtLmStatus } from './adapters/litertlm-adapter.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const repoRoot = path.resolve(__dirname, '..');
const staticRoot = path.resolve(repoRoot, process.env.OPALITE_STATIC_ROOT || '.');
const host = process.env.OPALITE_LOCAL_HOST || '0.0.0.0';
const port = Number(process.env.OPALITE_LOCAL_PORT || process.env.PORT || 8787);
const tlsCertPath = process.env.OPALITE_TLS_CERT;
const tlsKeyPath = process.env.OPALITE_TLS_KEY;
const defaultSystemPrompt = 'You are a local navigation assistant. Keep answers short and safety-first.';

const ollamaAdapter = new OllamaVisionAdapter({
  baseUrl: process.env.OPALITE_OLLAMA_URL || 'http://127.0.0.1:11434',
  model: process.env.OPALITE_OLLAMA_MODEL || process.env.OPALITE_LOCAL_MODEL || 'gemma3:4b'
});

const llamaCppAdapter = new LlamaCppVisionAdapter({
  baseUrl: process.env.OPALITE_LLAMACPP_URL || 'http://127.0.0.1:8082/v1',
  model: process.env.OPALITE_LLAMACPP_MODEL || 'gemma4-31b'
});

function json(response, statusCode, payload) {
  response.writeHead(statusCode, {
    'content-type': 'application/json; charset=utf-8',
    'cache-control': 'no-store'
  });
  response.end(JSON.stringify(payload, null, 2));
}

function getContentType(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  switch (ext) {
    case '.html': return 'text/html; charset=utf-8';
    case '.js': return 'text/javascript; charset=utf-8';
    case '.css': return 'text/css; charset=utf-8';
    case '.json': return 'application/json; charset=utf-8';
    case '.png': return 'image/png';
    case '.jpg':
    case '.jpeg': return 'image/jpeg';
    case '.gif': return 'image/gif';
    case '.webp': return 'image/webp';
    case '.svg': return 'image/svg+xml';
    case '.md': return 'text/markdown; charset=utf-8';
    default: return 'application/octet-stream';
  }
}

function safeJoin(root, unsafePath) {
  const requestedPath = unsafePath === '/' ? '/index.html' : unsafePath;
  const normalized = path.normalize(requestedPath).replace(/^([.][.][/\\])+/, '');
  const fullPath = path.resolve(root, `.${normalized}`);
  if (!fullPath.startsWith(root)) {
    throw new Error('Invalid path');
  }
  return fullPath;
}

async function collectBackendStatus() {
  const [llamacpp, ollama] = await Promise.all([
    llamaCppAdapter.getStatus(),
    ollamaAdapter.getStatus()
  ]);
  const litertlm = getLiteRtLmStatus();
  return {
    activeAdapter: llamacpp.available ? 'llamacpp' : (ollama.available ? 'ollama' : 'fallback'),
    llamacpp,
    ollama,
    litertlm,
    fallback: {
      available: true,
      adapter: 'fallback',
      reason: 'Used when no verified local model is reachable.'
    }
  };
}

async function selectAdapter() {
  const blockers = await collectBackendStatus();
  if (blockers.llamacpp.available) {
    return { adapter: llamaCppAdapter, backendStatus: blockers };
  }
  if (blockers.ollama.available) {
    return { adapter: ollamaAdapter, backendStatus: blockers };
  }
  return {
    adapter: new FallbackAdapter({ blockers }),
    backendStatus: blockers
  };
}

async function serveStatic(request, response) {
  const url = new URL(request.url, `http://${request.headers.host || 'localhost'}`);

  if (url.pathname === '/healthz') {
    json(response, 200, {
      ok: true,
      mode: 'opalite-local',
      protocol: tlsCertPath && tlsKeyPath ? 'https' : 'http',
      staticRoot,
      backend: await collectBackendStatus()
    });
    return;
  }

  let filePath;
  try {
    filePath = safeJoin(staticRoot, url.pathname);
  } catch {
    response.writeHead(403);
    response.end('Forbidden');
    return;
  }

  fs.readFile(filePath, (error, data) => {
    if (error) {
      response.writeHead(error.code === 'ENOENT' ? 404 : 500);
      response.end(error.code === 'ENOENT' ? 'Not found' : 'Server error');
      return;
    }

    response.writeHead(200, {
      'content-type': getContentType(filePath),
      'cache-control': 'no-store'
    });
    response.end(data);
  });
}

const server = tlsCertPath && tlsKeyPath
  ? https.createServer({
      cert: fs.readFileSync(path.resolve(repoRoot, tlsCertPath)),
      key: fs.readFileSync(path.resolve(repoRoot, tlsKeyPath))
    }, serveStatic)
  : http.createServer(serveStatic);

const wss = new WebSocketServer({ noServer: true, maxPayload: 8 * 1024 * 1024 });

function send(ws, payload) {
  if (ws.readyState === ws.OPEN) {
    ws.send(JSON.stringify(payload));
  }
}

async function handleTurn(session, message) {
  const prompt = message.prompt || message.text || 'Describe what matters for safe navigation in one or two short sentences.';
  const imageBase64 = message.image || session.latestFrame || null;
  const { adapter, backendStatus } = await selectAdapter();

  session.currentTurn += 1;
  const turnId = session.currentTurn;

  try {
    const result = await adapter.generateTurn({
      prompt,
      imageBase64,
      systemPrompt: session.systemPrompt || defaultSystemPrompt
    });

    if (turnId !== session.currentTurn) return;

    send(session.ws, {
      type: 'output_text',
      text: result.text,
      audioAttached: false,
      meta: {
        ...result.meta,
        backendStatus
      }
    });
    send(session.ws, { type: 'turn_complete' });
  } catch (error) {
    send(session.ws, {
      type: 'error',
      message: error.message || 'Local generation failed'
    });
  }
}

wss.on('connection', (ws) => {
  const session = {
    ws,
    latestFrame: null,
    systemPrompt: defaultSystemPrompt,
    audioChunksSeen: 0,
    currentTurn: 0,
    turnQueue: Promise.resolve()
  };

  ws.on('message', async (buffer, isBinary) => {
    if (isBinary) {
      send(ws, { type: 'error', message: 'Binary messages are not supported in this prototype.' });
      return;
    }

    let message;
    try {
      message = JSON.parse(buffer.toString());
    } catch {
      send(ws, { type: 'error', message: 'Invalid JSON message received.' });
      return;
    }

    switch (message.type) {
      case 'setup': {
        session.systemPrompt = message.systemPrompt || defaultSystemPrompt;
        const backendInfo = await collectBackendStatus();
        send(ws, {
          type: 'setup_complete',
          backendInfo,
          capabilities: {
            cameraInput: true,
            audioInput: true,
            speechToText: false,
            audioStreaming: false,
            browserSpeechFallback: true
          }
        });
        break;
      }
      case 'frame':
        session.latestFrame = message.image || null;
        break;
      case 'audio':
        session.audioChunksSeen += 1;
        break;
      case 'text':
      case 'describe':
        if (message.image) {
          session.latestFrame = message.image;
        }
        session.turnQueue = session.turnQueue
          .then(() => handleTurn(session, message))
          .catch((error) => {
            send(ws, { type: 'error', message: error.message || 'Turn handling failed' });
          });
        break;
      case 'interrupt':
        session.currentTurn += 1;
        send(ws, { type: 'interrupted' });
        break;
      case 'ping':
        send(ws, { type: 'pong', now: Date.now() });
        break;
      default:
        send(ws, { type: 'status', level: 'warn', message: `Unhandled message type: ${message.type}` });
    }
  });
});

server.on('upgrade', (request, socket, head) => {
  const url = new URL(request.url, `http://${request.headers.host || 'localhost'}`);
  if (url.pathname !== '/ws') {
    socket.destroy();
    return;
  }

  wss.handleUpgrade(request, socket, head, (ws) => {
    wss.emit('connection', ws, request);
  });
});

server.listen(port, host, () => {
  const protocol = tlsCertPath && tlsKeyPath ? 'https' : 'http';
  const wsProtocol = protocol === 'https' ? 'wss' : 'ws';
  console.log(`Opalite Local listening on ${protocol}://${host}:${port}`);
  console.log(`Static root: ${staticRoot}`);
  console.log(`WebSocket: ${wsProtocol}://${host}:${port}/ws`);
});
