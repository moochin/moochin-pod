# Pod Hub

A small box (an M5Stack Dial) that runs reaction-light drills on your BlazePods. No app, no account, no subscription, and no home Wi-Fi.

> Unofficial personal project. Not affiliated with or endorsed by BlazePod / PLAY COYOTTA LTD.
> **Once Pod Hub works, don't update your pods' firmware in the official app.** An update could change how the pods talk, and the hub would stop working.

**Status: milestone 1 (spike).** The hub finds the pods, connects, lights them and reads taps. Drills, the phone page and the one-click installer come in later milestones. This page will become a step-by-step guide for parents.

---

## Testing the milestone 1 spike

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
2. Switch your pods on.
3. Within a few seconds each pod should flash its number colour: 1 red, 2 green, 3 blue, 4 yellow. Its dot on the Dial lights up in the same colour.
4. **Press the Dial.** A random pod lights white. Tap it. The screen shows the reaction time the pod measured, and the log shows a line like:
   `TAP pod 2 lit-turned-off pod_ms=412 ...`
5. Switch a pod off and on again. It should reconnect by itself within a few seconds.
6. **Hold the Dial down for 1.5 seconds** to forget all pods and search again.

### What to send back

Copy the console log and send it, especially:

- the `found pod` lines (they show each pod's advertising data),
- the `GATT table` lines (they show whether the pods report their battery),
- any line saying **not responding** or **newer firmware**,
- the `heap free=` lines after a few minutes.
