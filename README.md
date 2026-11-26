# Pod Hub

A small box (an M5Stack Dial) that runs reaction-light drills on your BlazePods. No app, no account, no subscription, and no home Wi-Fi.

> Unofficial personal project. Not affiliated with or endorsed by BlazePod / PLAY COYOTTA LTD.
> **Once Pod Hub works, don't update your pods' firmware in the official app.** An update could change how the pods talk, and the hub would stop working.

**Status: milestone 3.** The hub is usable on its own: turn the knob to pick a drill, press to start, and see the results on the Dial. The full phone page, history and the one-click installer come in later milestones. This page will become a step-by-step guide for parents.

---

## Testing (milestones 1 and 2)

You need Chrome or Edge on a laptop, a USB-C cable, the Dial and your pods.

### 1. Get the firmware file

1. Open the repo on GitHub, then go to **Actions**, then **build**, then the latest green run.
2. Download **podhub-firmware** and unzip it. You need `podhub-factory.bin`.

### 2. Put it on the Dial (no terminal needed)

1. Plug the Dial into the laptop.
2. Open <https://espressif.github.io/esptool-js/> in Chrome.
3. Set **Baudrate** to 921600 and click **Connect**. Pick the port that looks like `USB JTAG/serial debug unit`.
   - If no port shows up, try another USB-C cable. Some cables only carry power.
4. Under **Flash Address** enter `0x0`, choose `podhub-factory.bin`, then click **Program**.
5. When it finishes, unplug the Dial and plug it back in.

### 3. Watch it work

1. On the same page, scroll to **Console**, set the baud rate to 115200 and click **Start**. This shows the hub's log.
2. **Fully close the BlazePod app** (swipe it away), or switch the phone's Bluetooth off. Pods connected to the app stop advertising and the hub can't see them.
3. Switch your pods on.
4. Within a few seconds each pod should flash its number colour: 1 red, 2 green, 3 blue, 4 yellow. Its dot on the Dial lights up in the same colour.
5. **Press the Dial.** A random pod lights white. Tap it. The screen shows the reaction time the pod measured, and the log shows a line like:
   `TAP pod 2 lit-turned-off pod_ms=412 ...`
6. Switch a pod off and on again. It should reconnect by itself within a few seconds.
7. **Hold the Dial down for 1.5 seconds** to forget all pods and search again.

### If it says "Searching..." and nothing connects

While searching, the Dial lists the strongest Bluetooth devices it can hear: name, signal strength and `m` (bytes of manufacturer data). Anything recognised as a pod is shown in **green**. Take a photo of the screen with the pods next to the Dial:

- **No pod names at all:** the pods aren't advertising. Check the app is closed, then switch the pods off and on.
- **Pods listed but not green:** they advertise differently from what the hub expects. The photo shows how, and I can fix it.

### Using the Dial

| Do this | What happens |
|---|---|
| **Turn** the knob on the home screen | Choose a drill: Reaction, Sprint, Sequence |
| **Press** | Start: 3-2-1 countdown with beeps, then go |
| **Press** during a drill | Stop early |
| **Turn** on the results | Results for each pod. **Press** to go back |
| **Hold 3 s** | Menu: player, drill length, sound, brightness, phone mode, find pods again, auto switch-off, demo pods, switch off |

The drills:

- **Reaction:** one random pod lights white. Hit it, and the next one lights 0.5 to 2 s later. Never the same pod twice in a row.
- **Sprint:** as many hits as you can, the next pod lights straight away.
- **Sequence:** pods light in order 1, 2, 3, 4, for conditioning circuits.

Each light gives you 3 s, or it counts as a miss (double beep). Drills last 30 s by default (change it in the menu: 20, 30, 45 or 60 s). The reaction time shown is the one the pod measured.

**Players:** choose who's training under **Menu → Player**. Personal bests are kept per player, per drill and per drill length, and the results screen flashes **PB!** when one is beaten. Players are called "Player 1" to "Player 4" until the phone page (milestone 4) lets you rename them.

**Switching off:** **Menu → Switch off**, then press to confirm. On battery that cuts the power completely. Switch it back on the way you normally do (the WAKE button). On USB power the Dial can't switch itself off, so it just goes dark until you unplug it. It also switches itself off after **15 minutes** unused, with a 30-second countdown first; any press or turn cancels it. Change or disable this under **Menu → Auto switch-off**. It never switches off during a drill or while a phone is connected.

**Battery level:** the Dial has no way to measure its battery, so the level isn't shown.

**Demo pods:** **Menu → Demo pods** simulates 4 pods with realistic reaction times, so you can try the drills with no pods. The Dial restarts to switch. Demo results never count as personal bests.

### Milestone 2: the 20-minute stability test

1. Flash the new firmware as above. Phone mode is **on** at first start.
2. Switch the pods on. Note how long it takes for all four dots to light. The log prints `all 4 pods connected ... ms after start`.
3. **Unplug the Dial and plug it back in.** Each pod should keep its number: pod 1 is still red, and so on.
4. **Join the hub from your iPhone.** Press twice to show the QR code (or open Settings → Wi-Fi → `PodHub`). The status page should open by itself. If it doesn't, open Safari and go to `192.168.4.1`.
5. **Press three times** to start the soak test. The hub lights a pod every couple of seconds. Tap some of them and leave the rest, so both hits and misses are counted.
6. Leave it running for **20 minutes or more**, with the phone page open part of the time. Partway through, switch one pod off for about 10 seconds and back on, then walk a pod away until it drops.
7. Check the bottom line on the Dial (`uptime  drops  free memory`) and the phone page.

**What good looks like:** no reboots (uptime keeps counting), free memory steady rather than falling, any dropped pod back within 10 seconds (`Worst gap` on the phone page), and all pods connected within 20 seconds of power-on.

### What to send back

Copy the console log and send it, especially:

- the `found pod` lines (they show each pod's advertising data),
- the `GATT table` lines (they show whether the pods report their battery),
- any line saying **not responding** or **newer firmware**,
- the `heap free=` lines after a few minutes,
- for milestone 2: the `all 4 pods connected` line, the `pod N: ... drops= worstGap=` summary from the end of the run, and a screenshot of the phone page.
