# Opalite PDR Demo Description and Demo Script

## PDR demo description

For the PDR demo, Opalite will be tested in a small indoor living-room environment that stands in for a realistic home navigation setting for a blind or low-vision user. The demo space includes a sectional sofa, a coffee table, and a workout bench placed in the middle of the room as a primary obstacle. Additional movable objects, such as a chair or exercise ball, can be added to create blocked paths and force the system to reason about nearby hazards.

During the demo, the user points the system into the room and Opalite uses RGB-D perception to identify major objects, detect obstacles in the walking path, and give short spoken guidance about what is directly ahead and where the safest open space is. The interaction also includes a short scene question, such as "What is in front of me?" or "Is there anything blocking the way?" so the demo shows both passive hazard awareness and active question answering.

The goal of this demo is not full autonomous navigation. The goal is to show that Opalite can understand a cluttered indoor scene well enough to warn about obstacles, describe the nearby layout, and support simple assistive guidance in real time.

## Demo script

### Scene setup
- Use the living room as the demo area.
- Keep the sectional sofa as a fixed boundary object.
- Place the coffee table in front of the sofa.
- Place the workout bench in the middle of the room as the main obstacle.
- Add one movable obstacle, like an exercise ball or chair, to partially block one path.

### Demo flow

#### 1. Opening
"This is Opalite, a spatial assistant for indoor navigation. In this demo, I am testing it in a living-room environment with furniture and obstacles placed in the walking path."

#### 2. Initial scene scan
Point the camera into the room and let Opalite observe the scene.

Expected behavior:
- identifies major objects such as sofa, coffee table, bench, or chair
- notices that the center path is partly blocked
- gives a short spoken summary

Example spoken output:
"There is a bench in the middle of the room, a coffee table ahead near the sofa, and open space slightly to the left."

#### 3. Hazard check
Move one step or slightly change the viewing angle so the blocked path is more obvious.

Prompt:
"Is there anything blocking the way?"

Expected behavior:
- names the obstacle in front
- gives a short directional cue

Example spoken output:
"Yes. A bench is blocking the center path. There is more open space to the left side."

#### 4. Object and layout query
Prompt:
"What is in front of me?"

Expected behavior:
- lists the most important nearby objects
- stays brief and navigation-focused

Example spoken output:
"In front of you there is a bench, a coffee table farther ahead, and a sofa behind it."

#### 5. Alternate obstacle test
Place or move a chair or exercise ball into a different part of the room.

Prompt:
"What changed?" or "Can I go forward?"

Expected behavior:
- notices the new obstacle
- updates the safe direction or warning

Example spoken output:
"A new obstacle is now on the right side. Forward movement is clearer on the left."

#### 6. Close
"This demo shows that Opalite can detect indoor obstacles, describe room structure, and answer short scene questions in a small home environment."

## What to emphasize during the PDR
- indoor assistive navigation, not full robotics
- obstacle awareness and short guidance
- real-world clutter instead of a clean toy scene
- simple, believable user questions
- one reliable demo path over a large feature set
