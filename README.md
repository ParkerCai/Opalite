# VisionGuide 👁️

**Real-time AI vision assistant for blind and low-vision users.**

VisionGuide uses the Gemini Live API to see through your phone's camera and describe the world around you — obstacles, signs, people, navigation cues — all through natural voice, hands-free.

> Built for the [Gemini Live Agent Challenge](https://geminiliveagentchallenge.devpost.com/) hackathon.

![VisionGuide Demo](assets/demo-placeholder.png)

## How It Works

```
Phone Camera → WebSocket → Gemini Live API → Audio Response → Speaker/Earbuds
```

One model. One API call. No separate OCR, no separate TTS, no separate STT. Gemini Live handles vision + voice together in real-time.

**The user experience:**
1. Open VisionGuide on your phone
2. Point the camera at the world
3. AI describes what it sees: "Door ahead, 10 feet. Sign reads EXIT. Person approaching from your right."
4. Ask questions by voice: "What color is that building?" — AI responds instantly
5. Haptic feedback for urgent hazards

## Quick Start

### Prerequisites
- A modern browser (Chrome, Safari, Edge) with camera + mic access
- A [Gemini API key](https://aistudio.google.com/apikey) (free tier works)

### Run Locally

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

### Deploy to Google Cloud

```bash
# Build and deploy to Cloud Run (static site)
gcloud run deploy vision-guide \
  --source . \
  --region us-central1 \
  --allow-unauthenticated
```

Or upload to a GCS bucket with static website hosting enabled.

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
│  │          VisionGuide Web App       │      │   │
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
| AI Model | Gemini 2.0 Flash (Live API) |
| Audio I/O | Web Audio API + AudioWorklet |
| Camera | getUserMedia API |
| Transport | WebSocket (bidirectional streaming) |
| Hosting | Any static file server / Google Cloud Run |

**Zero dependencies.** No npm, no React, no build step. Just open `index.html`.

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
├── index.html              # Main app page
├── css/styles.css          # Mobile-first responsive styles
├── js/
│   ├── app.js              # Main orchestrator
│   ├── gemini-client.js    # Gemini Live API WebSocket client
│   ├── audio-recorder.js   # Mic capture via AudioWorklet
│   ├── audio-streamer.js   # Real-time audio playback
│   ├── camera.js           # Camera capture + switching
│   └── worklets/
│       └── audio-processor.js  # AudioWorklet processor
├── README.md
├── DEVPOST.md              # Hackathon submission text
└── .gitignore
```

## Hackathon

**Gemini Live Agent Challenge** — Live Agents category 🗣️

- Category: Live Agents (Real-time Audio/Vision interaction)
- Mandatory tech: Gemini Live API, Google Cloud
- Submission deadline: March 16, 2026 @ 5:00 PM PDT

## License

MIT
