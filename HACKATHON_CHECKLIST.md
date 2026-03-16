# Opalite Hackathon Final Checklist
**Deadline: March 16, 2026 @ 5:00 PM PDT**

## ✅ COMPLETED (Ready to go!)
- [x] App development complete
- [x] Code committed and pushed to GitHub
- [x] Demo script written (`DEMO_SCRIPT.md`)
- [x] Devpost submission draft ready (`DEVPOST.md`)
- [x] Deployment configs created (Dockerfile, app.yaml, deploy.sh)

## 🔥 CRITICAL TASKS (Must complete today)

### 1. Deploy to GCP (30 min) - CAN DO NOW
```bash
cd ~/opalite

# One-time setup (if not done):
gcloud auth login
gcloud config set project YOUR_PROJECT_ID

# Deploy (automated script):
./deploy.sh
```
**Output needed:** Live HTTPS URLs for Devpost submission

### 2. Shoot Demo Video (45 min) - NEED DAYLIGHT
**Location:** Park near Dublin with SF city view (as planned in demo script)
**Equipment:** Phone + earbuds + steady hands
**Duration:** 2-3 minutes final cut
**Content:** Follow `DEMO_SCRIPT.md` exactly
- Scene 1: Walking navigation (60 sec)
- Scene 2: SF skyline description (60 sec)
- Intro/outro (30 sec total)

**Tech notes:**
- Record in landscape
- Use voice interaction (not tapping)
- Capture phone screen simultaneously
- Natural lighting for better AI recognition

### 3. Submit to Devpost (15 min) - AFTER DEPLOY + VIDEO
**URL:** https://geminiliveagentchallenge.devpost.com/
**Required:**
- Project description (use `DEVPOST.md`)
- Demo video (from step 2)
- Live demo URL (from step 1)
- GitHub repo link (already public)
- Team info
- Technologies used

## ⏰ TIME BUDGET (5:00 PM PDT deadline)
- **Deploy:** 30 min (can do morning/afternoon)
- **Video:** 45 min (needs good lighting, ~10 AM - 4 PM)
- **Devpost:** 15 min (final step)
- **Buffer:** 3+ hours for retakes/fixes

## 🎯 SUCCESS CRITERIA
1. **Live demo URL** working on mobile (HTTPS, camera, audio)
2. **Demo video** clearly shows real-time AI vision assistance
3. **Devpost submission** complete with all required fields
4. **Submitted before 5:00 PM PDT** ⚡

## 📱 Quick Test Checklist
After deployment, verify:
- [ ] Site loads on mobile browser
- [ ] Camera permission works
- [ ] Microphone permission works
- [ ] Gemini API connection works (enter test API key)
- [ ] Voice responses play back correctly
- [ ] No console errors

## 🚨 Emergency Contacts
- **Devpost support:** Available on hackathon Discord
- **Gemini API issues:** Check Google Cloud status page
- **Deployment issues:** Run `./deploy.sh` again or try manual commands in `DEPLOYMENT_GUIDE.md`

## 🎉 POST-SUBMISSION
- Update GitHub README with live demo link
- Share on social media
- Add to portfolio
- Celebrate! 🍾

---
*Everything is ready. You've got this! 💪*