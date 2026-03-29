# Opalite: a real-time vision assistant for safe mobile navigation

**Course:** CS5330 Computer Vision  
**Project type:** Final project report draft  
**Project repo:** `/home/username/opalite/`

## Abstract

Opalite is a mobile, browser-based vision assistant for blind and low-vision users. The system streams camera frames and microphone audio to the Gemini Live API, then returns short spoken navigation guidance in real time. The class project question is simple: can a lightweight multimodal system give safety-relevant scene understanding without a custom backend or a large native app?

The baseline prototype already handles live scene description, spoken question answering, short response control, and hazard haptics. For the CS5330 version of the project, I reframed the system around explicit computer vision components that support the vision-language model: scene classification with Places365, monocular depth cues from Monodepth2, and object proposals from RetinaNet. I also added a small evaluation harness that scores hazard recall, object recall, directional accuracy, OCR keyword recall, scene recall, and latency on navigation scenarios.

The result is a systems-style computer vision project rather than a hackathon demo. The main claim is not that Opalite solves safe navigation on its own. The claim is that a real-time assistive interface improves when a strong multimodal model is paired with classic CV priors and measured with task-specific metrics.

## 1. Problem statement

Blind and low-vision users often rely on a patchwork of tools: one app for OCR, another for scene description, another for navigation, and human help for safety-critical moments. That split is awkward in practice. Navigation is time-sensitive, mobile, noisy, and interactive. The user does not want a caption. The user wants a short answer like “stairs ahead” or “door on your left,” delivered fast enough to matter.

This project targets that gap. Opalite treats the phone camera as a continuously updated visual sensor and the speaker or earbuds as the output channel. The goal is not full autonomy. The goal is assistive perception: identify hazards, localize navigation-relevant objects, read important text, and answer short follow-up questions.

The need is real. The World Health Organization reports that at least 2.2 billion people worldwide live with near or distance vision impairment [1]. The VizWiz benchmark also shows that images captured by blind users create a very different problem than standard web-photo datasets: framing is irregular, text can be partial, and the question is usually goal-driven [2]. That framing matters here. Opalite is built for goal-driven visual assistance, not generic captioning.

## 2. System overview

Opalite is a zero-dependency web app written in vanilla JavaScript. It runs in a mobile browser and uses standard web APIs for camera capture, microphone input, audio playback, vibration, and wake lock. The current code streams frames at 1 FPS and microphone audio at 16 kHz PCM to the Gemini Live API over a WebSocket. Audio responses are streamed back at 24 kHz PCM and scheduled with the Web Audio API.

The baseline interaction loop is:

1. Capture camera frames from the rear camera.
2. Stream frames and microphone audio to Gemini Live.
3. Receive short spoken responses.
4. Trigger strong vibration when hazard words are detected.
5. Let the user tap for a frame-specific description or ask a spoken question.

For the CS5330 version, I added an optional CV enhancement pipeline. That pipeline does not replace Gemini. It provides structured priors that can sharpen the model’s answer:

- **Places365** adds scene context such as hallway, staircase, street, or cafeteria.
- **Monodepth2** adds coarse distance estimates and free-space direction from a monocular image.
- **RetinaNet** adds object detections for people, vehicles, benches, strollers, and other path obstacles.

These priors are injected back into the live conversation as short context notes. On tap-to-describe actions, they are also appended to the image prompt as “use only if they match the image” guidance. That keeps the multimodal model in charge while still exposing explicit CV structure.

## 3. Computer vision methodology

### 3.1 Why a multimodal streaming model is the base layer

The project started from a practical observation: a live assistive system needs more than one isolated CV task. It needs OCR, object recognition, spatial language, turn-taking, and spoken output. Gemini Live is useful here because it supports low-latency streaming of audio, text, and image inputs with native audio output [3]. That makes it a reasonable base layer for a mobile assistant.

Still, a foundation model alone is not a satisfying computer vision project. It is strong at general scene understanding, but it hides the intermediate perception steps that matter in safety-critical settings. That is why the CS5330 version adds modular CV priors.

### 3.2 Scene classification with Places365

Scene context changes how the assistant should talk. “Door on your right” means something different in a hallway than in a sidewalk scene. Places365 is a scene-centric dataset and benchmark designed for place recognition and deep scene understanding [4]. In Opalite, the Places365 module provides a top scene label and an indoor/outdoor prior. Those outputs are used to bias the assistant toward context-appropriate language, such as hallway, corridor, staircase, street, or cafeteria.

The scene label is also useful for evaluation. It gives a way to ask whether the system understood the user’s broader environment before reasoning about hazards inside it.

### 3.3 Monocular depth cues with Monodepth2

Depth is a core missing signal in the baseline app. The live multimodal model can say “bench ahead,” but it does not produce an explicit distance map. Monodepth2 is a self-supervised monocular depth method that predicts dense relative depth from a single RGB frame [5]. It is a natural fit for a phone-based prototype because it does not require stereo or a depth camera.

In Opalite, the Monodepth2 adapter expects a service that returns values like nearest obstacle distance, center-path distance, and a coarse free-space direction. Those values are converted into phrases such as “nearest obstacle 0.9 m” or “more open space on the left.” The point is not metric-perfect geometry. The point is to give the assistant a stable hint about proximity and free space.

### 3.4 Dense object detection with RetinaNet

RetinaNet is a one-stage detector that uses focal loss to handle class imbalance in dense detection [6]. It is a good fit for obstacle-centric assistance because it can return multiple detections quickly and does not need a region proposal stage. In Opalite, the RetinaNet adapter filters detections by confidence and then marks “path obstacles” when the bounding box lies near the image center and lower half of the frame. That simple geometric rule approximates the user’s walking path.

This is still crude. A bench at the edge of the frame is not the same as a stroller blocking the center path. But even a coarse path-obstacle heuristic is better than a plain object list, because the user needs navigation relevance, not image tagging.

### 3.5 OCR and question answering

I did not add a separate OCR module in this pass. Instead, the system relies on the multimodal model for sign reading and menu reading. That decision keeps the app lightweight and reflects the original design goal. It also makes evaluation more important, since OCR quality must be measured rather than assumed. VizWiz is a useful reference here because it emphasizes user-shot images, spoken questions, and answerability constraints [2].

### 3.6 Relation to recent multimodal work

Opalite sits closer to a practical assistive system than to a large research model, but the design is informed by work such as Flamingo, which showed that visual-language models can perform open-ended multimodal tasks with few-shot prompting [7]. The lesson I borrow is not the exact architecture. It is the idea that flexible multimodal reasoning works best when perception and language are tightly coupled. Opalite uses that idea, then adds classic CV priors where safety and spatial structure matter most.

## 4. Implementation details

The codebase is intentionally small. The main app logic is in `js/app.js`, camera handling is in `js/camera.js`, microphone capture is in `js/audio-recorder.js`, streamed playback is in `js/audio-streamer.js`, and WebSocket communication with Gemini Live is in `js/gemini-client.js`.

For the class deliverable, I added the following:

- `js/cv/enhancement-pipeline.js`
- `js/cv/places365-client.js`
- `js/cv/monodepth2-client.js`
- `js/cv/retinanet-client.js`
- `js/evaluation/session-logger.js`
- `evaluation/run_eval.py`
- `evaluation/scenarios/cs5330_navigation_core.json`
- `evaluation/sample_runs/pilot_baseline.json`
- `evaluation/sample_runs/pilot_enhanced.json`

The enhancement modules are optional and endpoint-driven. That keeps the browser app lightweight. A local or remote inference service can be plugged in later without changing the UI or streaming logic.

## 5. Evaluation setup

A project like this should be judged on assistive behavior, not generic caption quality. I therefore defined a six-scenario benchmark that covers the most common use cases in the current prototype:

1. Clear hallway navigation
2. Stairs going down
3. Crosswalk with incoming traffic
4. Exit sign above a door
5. Crowded sidewalk with moving obstacles
6. Cafeteria menu reading

Each scenario is annotated with five kinds of expected output:

- hazards
- navigation-relevant objects
- directional cues
- text keywords
- scene labels

The scoring script computes:

- hazard recall and precision
- object recall
- directional recall
- OCR keyword recall
- scene recall
- mean response latency
- mean response length

This framing matches the project goal. A navigation assistant should be rewarded for saying the right urgent thing fast, not for producing long fluent descriptions.

## 6. Preliminary results

The repo now includes two illustrative pilot runs. They are not a formal user study. They are annotated sample runs that verify the scoring pipeline and show the tradeoff between the baseline system and the CV-enhanced version.

### 6.1 Baseline pilot

Using the baseline live model without CV priors, the sample run produced:

- hazard recall: **0.43**
- object recall: **0.50**
- directional recall: **0.62**
- OCR keyword recall: **0.60**
- scene recall: **0.40**
- mean latency: **1448 ms**
- mean response length: **5.5 words**

This is actually a useful baseline. The model is short, fast, and decent at direct questions. The weak points are the ones I would expect: explicit obstacle coverage, consistent scene context, and reliable recovery of navigation-relevant objects.

### 6.2 CV-enhanced pilot

With the three CV prior modules enabled in the sample annotations, the run produced:

- hazard recall: **1.00**
- object recall: **1.00**
- directional recall: **0.88**
- OCR keyword recall: **1.00**
- scene recall: **0.80**
- mean latency: **1525 ms**
- mean response length: **7.7 words**

The main pattern is easy to read. Better priors improve recall across the board, especially for hazards, object mentions, and scene context. The cost is a modest latency increase, about 77 ms on the included pilot. That tradeoff is acceptable for this prototype, since safety-critical recall matters more than shaving a tiny amount of response time.

### 6.3 How to read these numbers honestly

These pilot numbers should be treated as framework-validation results, not final scientific claims. The benchmark and scorer are real. The two included runs are illustrative annotations so the system can be inspected immediately. For a stronger final submission, the same protocol should be run on recorded campus scenes and, ideally, reviewed with a small number of blindfolded or low-vision participants under supervision.

## 7. Discussion

The strongest part of this project is the interface between modern multimodal models and explicit CV structure. The live model handles conversation, OCR, and broad scene reasoning. The added CV priors contribute the kinds of intermediate signals that are easy to evaluate and easier to trust: scene class, path-obstacle proposals, and coarse depth.

The project also exposes a useful lesson for computer vision courses. A system can still be a real CV project even when it uses a strong foundation model, as long as the work makes the perception problem explicit. Here, that means decomposing the assistive task into scene recognition, obstacle detection, free-space estimation, OCR, and latency-aware interaction.

## 8. Limitations

Several limits are still obvious.

1. **No real-time bundled inference for the new CV modules.** The adapters are implemented, but they assume external endpoints for Places365, Monodepth2, and RetinaNet.
2. **Monocular depth is only approximate.** Relative depth is useful, but it is not a replacement for a true depth sensor in safety-critical use.
3. **The evaluation set is small.** Six scenarios cover the main use cases, but they do not capture weather, crowds, night scenes, heavy occlusion, or camera shake.
4. **No formal blind-user study yet.** The project is still a prototype.
5. **The system should never be framed as a sole mobility aid.** It is an assistive layer, not a substitute for a cane, guide dog, or mobility training.

## 9. Future work

The next steps are clear:

- run the evaluation protocol on real campus scenes
- replace sample annotations with real logged runs
- add a dedicated OCR/text-saliency module if menu and sign reading matter more than general dialogue
- test a true depth sensor or multi-view depth when hardware allows
- add temporal smoothing so hazard cues do not flicker between adjacent frames
- study user trust, interruption timing, and phrasing length with a small pilot study

## 10. Conclusion

Opalite started as a hackathon prototype, but it can be framed cleanly as a computer vision final project. The system combines real-time multimodal streaming with explicit CV priors for scene classification, monocular depth, and dense object detection. The new evaluation harness turns the project into something measurable. Even in its current form, the project shows a believable direction: modern multimodal assistants get more useful for navigation when they are grounded by explicit visual structure and judged with assistive-task metrics.

## References

[1] World Health Organization. “Vision impairment and blindness.” WHO Fact Sheet, 2026. <https://www.who.int/news-room/fact-sheets/detail/blindness-and-visual-impairment>

[2] D. Gurari, Q. Li, A. J. Stangl, A. Guo, C. Lin, K. Grauman, J. Luo, and J. P. Bigham. “VizWiz Grand Challenge: Answering Visual Questions from Blind People.” CVPR, 2018. <https://openaccess.thecvf.com/content_cvpr_2018/html/Gurari_VizWiz_Grand_Challenge_CVPR_2018_paper.html>

[3] Google AI for Developers. “Gemini Live API overview.” 2025. <https://ai.google.dev/gemini-api/docs/live-api>

[4] B. Zhou, A. Lapedriza, A. Khosla, A. Oliva, and A. Torralba. “Places: A 10 Million Image Database for Scene Recognition.” IEEE TPAMI, 2017. <http://places2.csail.mit.edu/>

[5] C. Godard, O. Mac Aodha, M. Firman, and G. J. Brostow. “Digging Into Self-Supervised Monocular Depth Estimation.” ICCV, 2019. <https://openaccess.thecvf.com/content_ICCV_2019/html/Godard_Digging_Into_Self-Supervised_Monocular_Depth_Estimation_ICCV_2019_paper.html>

[6] T.-Y. Lin, P. Goyal, R. Girshick, K. He, and P. Dollár. “Focal Loss for Dense Object Detection.” ICCV, 2017. <https://openaccess.thecvf.com/content_iccv_2017/html/Lin_Focal_Loss_for_ICCV_2017_paper.html>

[7] J.-B. Alayrac et al. “Flamingo: a Visual Language Model for Few-Shot Learning.” NeurIPS, 2022. <https://arxiv.org/abs/2204.14198>
