# Opalite — Demo-First Plan

## The Demo Story (2-3 min video)

### Use Case 1: "Getting to Class" (Indoor Navigation)
A blind student needs to get from a building entrance to a classroom.

**Scene flow:**
1. User opens Opalite. AI introduces itself: "Opalite active. I can see ahead of you."
2. Camera points at a hallway → AI: "Long hallway ahead. Floor is clear. Doors on both sides."
3. User walks → AI: "Door on your right. Sign reads Room 204."
4. Approaching stairs → AI (urgent): "Stairs ahead, 5 feet. Going down, about 10 steps. Handrail on your right."
5. User asks: "Is there an elevator nearby?" → AI: "I see an elevator sign to your left, about 15 feet."
6. At destination → AI: "Door ahead. Sign reads Room 101. The door is closed."

**Why this wins:** Safety + navigation + text reading all in one scene. Shows proactive hazard detection.

### Use Case 2: "Reading a Menu" (Daily Life)
A blind person at a coffee shop.

**Scene flow:**
1. User points phone at the counter → AI: "You're at a counter. Menu board above. Let me read it."
2. AI reads menu items and prices naturally
3. User asks: "What's the cheapest coffee?" → AI answers
4. User points at their wallet → AI: "I can see a $10 bill and a $5 bill."

**Why this wins:** Independence in daily life. Shows reading + reasoning, not just description.

## Demo Shooting Plan

**Where:** NEU campus building (hallways, stairs, signs, doors) + nearby coffee shop
**When:** Sunday afternoon (good lighting)
**Equipment:** Phone on a lanyard or held chest-height, earbuds in
**Duration:** 2-3 minutes, edited from ~10 min of raw footage

**Shot list:**
1. Opening title card (app icon + name)
2. Phone screen showing app starting
3. First-person POV walking through building
4. Close-up of AI reacting to stairs (safety moment)
5. Reading signs/text in real-time
6. Voice interaction (user asking question)
7. Closing: statistics about vision impairment

## App Behavior — What Needs to Change

### Proactivity Mode (DEFAULT ON)
The AI should narrate continuously without being prompted:
- Describe new environments when entering them
- Call out obstacles and hazards immediately
- Read visible text automatically
- Comment on changes in the scene
- Go silent only when nothing changes for a while

### Periodic Nudge
If AI hasn't spoken in 5-8 seconds, send a silent prompt:
"Describe any changes you see. If nothing changed, stay quiet."

### Urgency Levels
- **URGENT** (stairs, traffic, obstacles): Interrupt immediately + haptic
- **NORMAL** (doors, signs, people): Speak at natural pace
- **LOW** (ambient details): Only when scene is quiet

## Submission Checklist

- [ ] Working app deployed on GCP (Cloud Run)
- [ ] Public GitHub repo with README + setup instructions
- [ ] Demo video (2-3 min)
- [ ] Architecture diagram (PNG/SVG)
- [ ] GCP deployment proof (screen recording)
- [ ] Devpost text submission
- [ ] Blog post (bonus points)

## Timeline

**Saturday night (now):**
- [x] Core app working
- [ ] Fix proactivity (system prompt + periodic nudge)
- [ ] Test improved behavior

**Sunday morning:**
- [ ] Polish UI
- [ ] Deploy to GCP Cloud Run
- [ ] Screen-record GCP deployment proof

**Sunday afternoon:**
- [ ] Shoot demo video at NEU campus + coffee shop
- [ ] Edit video (can use phone editor or CapCut)

**Sunday evening (before 5 PM PDT):**
- [ ] Push to GitHub
- [ ] Submit on Devpost
- [ ] Architecture diagram
