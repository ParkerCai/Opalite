#!/bin/bash
# Opalite GCP Deployment Script
# Run this after setting up gcloud CLI and authenticating

set -e  # Exit on error

echo "🚀 Deploying Opalite to Google Cloud..."

# Check if gcloud is authenticated
if ! gcloud auth list --filter=status:ACTIVE --format="value(account)" | head -1 > /dev/null; then
    echo "❌ Not authenticated with gcloud. Run: gcloud auth login"
    exit 1
fi

# Get current project
PROJECT=$(gcloud config get-value project)
if [ -z "$PROJECT" ]; then
    echo "❌ No GCP project set. Run: gcloud config set project YOUR_PROJECT_ID"
    exit 1
fi

echo "📋 Using project: $PROJECT"

# Option 1: Deploy to Cloud Run (recommended for hackathon - fast, scalable)
echo "🐋 Deploying to Cloud Run..."
gcloud run deploy opalite \
    --source . \
    --platform managed \
    --region us-central1 \
    --allow-unauthenticated \
    --port 8080 \
    --memory 512Mi \
    --cpu 1 \
    --min-instances 0 \
    --max-instances 10

echo ""
echo "✅ Cloud Run deployment complete!"
CLOUD_RUN_URL=$(gcloud run services describe opalite --platform managed --region us-central1 --format 'value(status.url)')
echo "🌍 Live at: $CLOUD_RUN_URL"

# Option 2: Also deploy to App Engine (backup option)
echo ""
echo "🌐 Also deploying to App Engine (backup)..."
gcloud app deploy --quiet

APP_ENGINE_URL="https://$PROJECT.uc.r.appspot.com"
echo "🌍 App Engine backup: $APP_ENGINE_URL"

echo ""
echo "🎉 Deployment complete! Both endpoints are live:"
echo "   Primary (Cloud Run): $CLOUD_RUN_URL"
echo "   Backup (App Engine): $APP_ENGINE_URL"
echo ""
echo "🔗 Add these URLs to your Devpost submission!"