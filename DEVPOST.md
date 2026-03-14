# VisionGuide — Devpost Submission

## Inspiration

2.2 billion people worldwide have a vision impairment (WHO, 2023). Existing assistive tools are fragmented — separate apps for text recognition, object detection, navigation. None of them talk to you naturally in real-time.

We asked: what if a blind person could just point their phone at the world and have an AI companion describe everything — obstacles, signs, people, navigation cues — through natural conversation? Not a text readout. Not a button press. Just a friend walking beside you, always watching.

The Gemini Live API made this possible for the first time: real-time vision AND voice in a single model, with interruption support for natural conversation.

## What It Does

VisionGuide turns any smartphone into a real-time AI vision assistant:

- **Sees through your camera** — captures frames from the rear camera at 2-5 FPS and sends them to Gemini
- **Speaks what it sees** — "Door ahead, 10 feet. Steps going down, 3 steps. Sign reads Emergency Exit."
- **Listens and responds** — ask "What's to my left?" and get an immediate spoken answer
- **Prioritizes safety** — hazards like traffic, stairs, and obstacles are called out immediately with haptic feedback
- **Remembers context** — doesn't repeat itself. Tracks what's already been described and focuses on changes
- **Fully hands-free** — no screen interaction needed once started. Just point and listen.

## How We Built It

**The key insight:** Gemini Live API handles vision + voice in ONE model. No separate OCR pipeline. No separate TTS. No separate STT. One WebSocket connection does everything.

Architecture:
```
Phone Camera (2 FPS JPEG frames) ──┐
                                    ├──→ WebSocket ──→ Gemini Live API ──→ Audio response
Phone Microphone (PCM16 audio) ────┘                                        ↓
                                                                    Phone Speaker
```

Built with:
- **Vanilla JavaScript** — zero dependencies, no build step, no npm. Works with `python -m http.server`
- **Gemini 2.0 Flash Live API** — real-time bidirectional streaming via WebSocket
- **Web Audio API + AudioWorklet** — low-latency audio capture and playback
- **getUserMedia** — camera and microphone access
- **Screen Wake Lock API** — prevents screen timeout during use
- **Vibration API** — haptic feedback for hazard alerts

The entire frontend is ~25KB of code. No backend server needed — the browser connects directly to the Gemini API.

## Challenges We Ran Into

1. **Audio synchronization** — PCM16 audio chunks arrive asynchronously. We built a buffering/scheduling system using Web Audio API's precise timing to prevent gaps and clicks.

2. **Camera frame rate balance** — 30 FPS overwhelms the API. Too slow and the AI misses things. We settled on configurable 1-5 FPS with 2 FPS as default.

3. **Mobile browser quirks** — iOS requires `playsInline` on video elements. Chrome on Android handles AudioContext suspension differently. Camera `facingMode` behaves differently across devices.

4. **Echo cancellation** — Without careful audio routing, the AI would hear its own voice through the phone speaker and enter a feedback loop. Web Audio API echo cancellation flags solved this.

## Accomplishments We're Proud Of

- **Zero-dependency architecture** — the entire app is vanilla JS that runs from a static file server. No build step, no framework, no node_modules. Judges can test it in 30 seconds.
- **It actually works in real-time** — walking around with VisionGuide describing the world is genuinely useful and a little magical.
- **Truly hands-free** — once started, the user never needs to touch the screen again. Wake lock, auto-mute detection, and proactive descriptions handle everything.

## What We Learned

- The Gemini Live API is remarkably capable at real-time vision understanding — it can read signs, identify objects, track spatial relationships, and describe scenes with surprising accuracy.
- Simple architecture wins. One model, one WebSocket, one API call. The best assistive tech is the kind you don't have to think about.
- Accessibility features aren't just for the target users — high contrast, large touch targets, and clear status indicators make the app better for everyone.

## What's Next

- **Depth camera integration** — Intel RealSense D435 for precise distance measurements ("obstacle 3.2 feet ahead")
- **Wearable form factor** — mount on glasses or a lanyard for true hands-free use
- **Offline fallback** — on-device vision model for basic obstacle detection without internet
- **Navigation mode** — GPS + vision integration for turn-by-turn walking directions
- **Multi-language support** — Gemini supports 100+ languages natively
- **Community features** — crowdsourced location descriptions for frequently visited places

## Built With

- Gemini Live API (gemini-2.0-flash-live-001)
- Google GenAI SDK (WebSocket)
- JavaScript (vanilla, ES modules)
- Web Audio API
- Google Cloud (deployment)
- HTML5 / CSS3
