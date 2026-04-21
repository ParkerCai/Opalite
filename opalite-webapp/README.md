# Opalite 👁️

**Real-time AI vision assistant for blind and low-vision users.**

Opalite uses the Gemini Live API to see through your phone's camera and describe the world around you, obstacles, signs, people, navigation cues, all through natural voice, hands-free.

It now also includes an **Opalite Local** prototype path that keeps the same browser UI but swaps in a local WebSocket backend inspired by Parlor.

> Built for the [Gemini Live Agent Challenge](https://geminiliveagentchallenge.devpost.com/) hackathon.

![Opalite Demo](assets/demo-placeholder.png)

## How It Works

```
Gemini mode: Phone Camera → WebSocket → Gemini Live API → Audio Response → Speaker/Earbuds
Local mode:  Phone Camera + Mic → WebSocket → Opalite Local backend → local model adapter → spoken/text response
```

One model. One API call. No separate OCR, no separate TTS, no separate STT. Gemini Live handles vision + voice together in real-time.

**The user experience:**
1. Open Opalite on your phone
2. Point the camera at the world
3. AI describes what it sees: "Door ahead, 10 feet. Sign reads EXIT. Person approaching from your right."
4. Ask questions by voice: "What color is that building?" — AI responds instantly
5. Haptic feedback for urgent hazards

## Quick Start

### Prerequisites
- A modern browser (Chrome, Safari, Edge) with camera + mic access
- A [Gemini API key](https://aistudio.google.com/apikey) (free tier works) for Gemini mode
- Node.js 22+ for the Opalite Local backend

### Run Gemini mode locally

```bash
git clone https://github.com/ParkerCai/vision-guide.git
cd vision-guide
python3 -m http.server 8000
```

Open `https://localhost:8000` on your phone (must be HTTPS or localhost for camera access).

**For phone testing over local network:**
```bash
# Generate a quick self-signed cert
openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 1 -nodes -subj '/CN=localhost'

# Serve with HTTPS
python3 -c "
import ssl, http.server
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain('cert.pem', 'key.pem')
srv = http.server.HTTPServer(('0.0.0.0', 8443), http.server.SimpleHTTPRequestHandler)
srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
print('Serving on https://0.0.0.0:8443')
srv.serve_forever()
"
```

Then open `https://<your-ip>:8443` on your phone and accept the certificate warning.

### Run the Opalite Local prototype

```bash
cd /home/username/opalite/backend
npm install
npm start
```

Then open `http://localhost:8787`, switch the setup screen to **Opalite Local (prototype)**, and tap the camera view to request a description.

For the fuller local-mode notes, see [`docs/LOCAL_MODE.md`](docs/LOCAL_MODE.md).

### Deploy to Google Cloud

```bash
# Build and deploy to Cloud Run (static site)
gcloud run deploy vision-guide \
  --source . \
  --region us-central1 \
  --allow-unauthenticated
```

Or upload to a GCS bucket with static website hosting enabled.

## Modes

### Gemini Live

- Existing direct browser → Gemini Live path remains intact
- Best current option for full voice interaction

### Opalite Local prototype

- Adds a local backend in `backend/`
- Streams camera frames and mic audio over WebSocket
- Prefers a real local multimodal adapter when available
- Falls back to a connectivity-only local response when no model is installed
- Uses browser speech synthesis as the current audio fallback for local mode

## Architecture

```
┌─────────────────────────────────────────────────┐
│                   User's Phone                   │
│                                                  │
│  ┌──────────┐  ┌──────────┐  ┌───────────────┐  │
│  │  Camera   │  │   Mic    │  │   Speaker/    │  │
│  │ (rear)    │  │ (voice)  │  │   Earbuds     │  │
│  └────┬─────┘  └────┬─────┘  └───────▲───────┘  │
│       │              │                │          │
│  ┌────▼──────────────▼────────────────┤──────┐   │
│  │          Opalite Web App       │      │   │
│  │  • Camera frames (JPEG, 2 FPS)    │      │   │
│  │  • Audio chunks (PCM16, 16kHz)    │      │   │
│  │  • Audio playback (PCM16, 24kHz)  │      │   │
│  └────┬───────────────────────────────┘──────┘   │
│       │              WebSocket                   │
└───────┼──────────────────────────────────────────┘
        │
        ▼
┌───────────────────────────────────────┐
│     Gemini Live API (Google Cloud)    │
│                                       │
│  Model: gemini-2.0-flash-live-001     │
│  • Real-time vision understanding     │
│  • Natural language voice output      │
│  • Context-aware descriptions         │
│  • Interruptible conversation         │
└───────────────────────────────────────┘
```

## Tech Stack

| Component | Technology |
|-----------|-----------|
| Frontend | Vanilla JavaScript (ES modules, no build step) |
| AI Model | Gemini Live or local backend adapter |
| Audio I/O | Web Audio API + AudioWorklet |
| Camera | getUserMedia API |
| Transport | WebSocket (bidirectional streaming) |
| Hosting | Static file server, bundled local backend, or Google Cloud Run |

**Frontend remains build-step free.** Local mode adds a small Node backend in `backend/`.

## Features

- 🎯 **Real-time scene description** — AI narrates what the camera sees
- 🗣️ **Natural voice interaction** — ask questions, get spoken answers
- 🔄 **Interruptible** — speak over the AI and it stops to listen
- 📱 **Mobile-first** — designed for phones, works on desktop too
- ♿ **Accessibility-focused** — high contrast, large touch targets, ARIA labels
- 📳 **Haptic feedback** — vibration alerts for hazards
- 🔒 **Screen wake lock** — prevents screen from sleeping during use
- 🔄 **Camera switching** — front/back camera toggle
- 🎚️ **Adjustable FPS** — balance between detail and bandwidth

## Project Structure

```
vision-guide/
├── backend/
│   ├── server.js               # Local HTTP + WebSocket server
│   └── adapters/               # Local model adapter scaffolding
├── index.html                  # Main app page
├── css/styles.css              # Mobile-first responsive styles
├── js/
│   ├── app.js                  # Main orchestrator
│   ├── gemini-client.js        # Gemini Live API WebSocket client
│   ├── local-client.js         # Opalite Local WebSocket client
│   ├── audio-recorder.js       # Mic capture via AudioWorklet
│   ├── audio-streamer.js       # Real-time audio playback
│   ├── camera.js               # Camera capture + switching
│   └── worklets/
│       └── audio-processor.js  # AudioWorklet processor
├── docs/LOCAL_MODE.md
├── README.md
├── DEVPOST.md                  # Hackathon submission text
└── .gitignore
```

## Hackathon

**Gemini Live Agent Challenge** — Live Agents category 🗣️

- Category: Live Agents (Real-time Audio/Vision interaction)
- Mandatory tech: Gemini Live API, Google Cloud
- Submission deadline: March 16, 2026 @ 5:00 PM PDT

## License

MIT
