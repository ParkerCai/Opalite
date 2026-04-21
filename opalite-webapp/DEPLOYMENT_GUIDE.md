# Opalite Deployment Guide

## Quick Deploy (for Parker)

**Prerequisites (one-time setup):**
```bash
# Install gcloud CLI if not already installed
curl https://sdk.cloud.google.com | bash
exec -l $SHELL

# Authenticate and set project
gcloud auth login
gcloud config set project YOUR_PROJECT_ID  # Use your GCP project ID
```

**Deploy (hackathon day):**
```bash
cd ~/opalite
./deploy.sh
```

This will deploy to both Cloud Run (primary) and App Engine (backup) and give you live URLs for Devpost submission.

## Manual Deploy Options

### Option 1: Cloud Run (Recommended)
```bash
gcloud run deploy opalite \
    --source . \
    --platform managed \
    --region us-central1 \
    --allow-unauthenticated
```

### Option 2: App Engine
```bash
gcloud app deploy
```

### Option 3: Firebase Hosting
```bash
npm install -g firebase-tools
firebase login
firebase init hosting
firebase deploy
```

## Testing Deployment

After deployment, test these key features:
1. Camera access (must be HTTPS)
2. Microphone access
3. Gemini API connection (need to add your API key)
4. Audio playback

## Environment Variables

The app needs a Gemini API key. Options:

1. **Client-side (current)**: User enters API key in the UI
2. **Server-side**: Add backend to proxy API calls
3. **Build-time**: Embed key at build time (not recommended for public repos)

For the hackathon, client-side is fine since judges will have their own keys.

## Troubleshooting

**"Camera access denied"**
- Must be served over HTTPS
- User must grant permissions

**"WebSocket connection failed"**
- Check API key validity
- Verify Gemini Live API is enabled in GCP project

**"Audio not working"**
- Check autoplay policies
- Test on different browsers/devices