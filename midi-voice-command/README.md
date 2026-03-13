# MIDI Voice Command

Control a MIDI device via Google Home voice commands. Say "Hey Google, activate slot 1" and a MIDI CC message is sent to the connected MIDI device.

Runs 24/7 as a macOS background service.

---

## How It Works

1. You say a voice command to Google Home
2. Google routes it to a fulfillment server running on your Mac (via Cloudflare Tunnel)
3. The server sends a MIDI CC message to the configured MIDI device

---

## Prerequisites

- macOS (tested on 26.3 Tahoe)
- [Node.js](https://nodejs.org) v18 or later
- [Homebrew](https://brew.sh)
- A [Cloudflare](https://cloudflare.com) account with a domain managed by Cloudflare
- A Google account with a Google Home device
- A MIDI device connected to the Mac

---

## Installation

### 1. Clone and install dependencies

```bash
git clone https://github.com/collinp2/cp_software.git
cd cp_software
git checkout claude/midi-voice-command
cd midi-voice-command
npm install
```

### 2. Create config.json

```bash
cp config.example.json config.json
```

Edit `config.json`:

```json
{
  "midiDevice": "",
  "midiChannel": 1,
  "port": 3000,
  "oauth": {
    "clientId": "midi-client",
    "clientSecret": "generate-with-command-below",
    "accessToken": "generate-with-command-below"
  },
  "commands": {
    "slot 1": { "cc": 50, "value": 127 },
    "slot 2": { "cc": 51, "value": 127 },
    "slot 3": { "cc": 52, "value": 127 },
    "slot 4": { "cc": 53, "value": 127 },
    "slot 5": { "cc": 54, "value": 127 }
  }
}
```

**Find your MIDI device name:**
```bash
node -e "const m=require('midi'),o=new m.Output(); for(let i=0;i<o.getPortCount();i++) console.log(i,o.getPortName(i))"
```
Copy the exact device name (e.g. `USB MIDI   Port 1`) into `midiDevice`.

**Generate OAuth tokens** (run twice — one for each field):
```bash
node -e "console.log(require('crypto').randomBytes(32).toString('hex'))"
```

> **Important:** `config.json` is not committed to git — it contains secrets. Keep it safe.

---

### 3. Set up Cloudflare Tunnel

This gives the server a permanent public HTTPS URL so Google can reach it.

**Install cloudflared:**
```bash
brew install cloudflare/cloudflare/cloudflared
```

> **No Homebrew / no sudo?** See the [cloudflared as a user LaunchAgent](#cloudflared-as-a-user-launchagent) section below.

**Create a named tunnel via the Zero Trust dashboard:**
1. Go to [one.dash.cloudflare.com](https://one.dash.cloudflare.com) → Networks → Tunnels
2. Click **Create a tunnel** → select **Cloudflared** → name it `midi-voice`
3. On the next screen, configure a public hostname:
   - Subdomain: `midi` (or any subdomain you choose)
   - Domain: your Cloudflare domain
   - Service type: `HTTP`
   - URL: `localhost:3000`
4. Copy the install command shown (starts with `sudo cloudflared service install ...`)
5. Run it in your terminal — this installs cloudflared as a system daemon that starts at boot

**Verify the tunnel is working:**
```bash
curl https://midi.yourdomain.com/commands
```

> **Only run cloudflared on one Mac at a time.** If you move the app to a new machine, stop cloudflared on the old one first. Running two connectors on the same tunnel causes intermittent 502 errors as Cloudflare load-balances between them. See [Troubleshooting](#troubleshooting) for details.

---

### 4. Set up Google Home integration

> If you are moving this app to a new Mac but keeping the same domain and config.json, skip to step 4d — the Google Home project and account linking carry over.

**4a. Create a Google Home project:**
1. Go to [console.home.google.com](https://console.home.google.com)
2. Create a new project
3. Add a **Cloud-to-cloud** integration
4. Integration name: `MIDI Voice`, Device type: `Scene`
5. Upload an app icon (144×144 PNG, filename must be the project ID)

**4b. Fill in OAuth & fulfillment URLs:**
- Client ID: `midi-client` (must match config.json)
- Client secret: (from config.json)
- Authorization URL: `https://midi.yourdomain.com/oauth`
- Token URL: `https://midi.yourdomain.com/oauth/token`
- Fulfillment URL: `https://midi.yourdomain.com/fulfillment`

**4c. Enable testing:**
Click the **Test** tab → **Ready to test**

**4d. Link the integration:**
1. Open the **Google Home** app on your phone
2. Tap **+** → **Set up device** → **Works with Google**
3. Search for **MIDI Voice** and sign in

---

### 5. Install the LaunchAgent (autostart at login)

```bash
# Copy the plist — update the username if it differs from collinpeterson
cp com.collinpeterson.midi-voice-command.plist ~/Library/LaunchAgents/

# Edit the plist if your username is different
# Change /Users/collinpeterson to /Users/yourusername in all paths
nano ~/Library/LaunchAgents/com.collinpeterson.midi-voice-command.plist

# Load it
launchctl load ~/Library/LaunchAgents/com.collinpeterson.midi-voice-command.plist
```

The server will now start automatically at login and restart if it crashes.

---

## Pro Tools Transport Control (Optional)

Voice commands can control Pro Tools transport (play, stop, record) via AppleScript keystrokes.

### Setup

**1. Enable IAC Driver buses in Audio MIDI Setup:**

Open `/System/Applications/Utilities/Audio MIDI Setup.app` → Window → Show MIDI Studio → double-click IAC Driver → add at least 2 buses (Bus 2 and Bus 3) → check "Device is online".

**2. Configure Pro Tools HUI peripheral:**

In Pro Tools: Setup → Peripherals → MIDI Controllers tab → add a controller:
- Type: `HUI`
- Receive From: `IAC Driver Bus 3`
- Send To: `IAC Driver Bus 2`
- \# Ch's: `8`

This keeps Pro Tools in sync with the server (ping/pong keepalive). Restart Pro Tools after saving.

**3. Add fields to config.json:**

```json
{
  "huiOutputDevice": "IAC Driver Bus 3",
  "huiInputDevice": "IAC Driver Bus 2",
  "mmcOutputDevice": "IAC Driver Bus 1"
}
```

**4. Grant Accessibility permission to Node:**

The server uses AppleScript to send keystrokes to Pro Tools. macOS requires an Accessibility permission for this.

Go to System Settings → Privacy & Security → Accessibility → click **+** and add your Node.js binary (e.g. `/opt/homebrew/bin/node`).

To find your Node binary path:
```bash
which node
```

**5. Add transport commands to config.json:**

```json
"commands": {
  "play": { "os": "play" },
  "stop": { "os": "stop" },
  "start recording": { "os": "record_start" },
  "stop recording": { "os": "stop" }
}
```

Restart the server after updating config.json. Say "Hey Google, sync my devices" so Google picks up the new commands.

---

## Usage

**Kemper slot selection (CC):**

| Say | MIDI CC |
|-----|---------|
| "Hey Google, activate slot 1" | CC 50 value 127 |
| "Hey Google, activate slot 2" | CC 51 value 127 |
| "Hey Google, activate slot 3" | CC 52 value 127 |
| "Hey Google, activate slot 4" | CC 53 value 127 |
| "Hey Google, activate slot 5" | CC 54 value 127 |

**Kemper effects (CC):**

| Say | CC | Action |
|-----|----|--------|
| "activate Toggle all effects" | CC 16 | Toggle all |
| "activate A module on/off" | CC 17 | Stomp A |
| "activate B module on/off" | CC 18 | Stomp B |
| "activate C module on/off" | CC 19 | Stomp C |
| "activate D module on/off" | CC 20 | Stomp D |
| "activate X module on/off" | CC 22 | Stomp X |
| "activate Modulation on/off" | CC 24 | Mod |
| "activate DELAY on/off" | CC 27 | Delay |
| "activate REVERB on/off" | CC 29 | Reverb |
| "activate Tuner on/off" | CC 31 | Tuner |
| "activate All effects off" | multi-CC | All off (50ms stagger) |
| "activate Looper start" | CC 81 | Looper start |
| "activate Looper stop" | CC 82 | Looper stop |

**Kemper performances (Program Change):**

Each command sends a PC message to load slot 1 of that performance. Both the numbered name and alias work.

| Say (either) | Alias | PC |
|---|---|---|
| "activate performance 1" | Little King | 0 |
| "activate performance 2" | EVH | 5 |
| "activate performance 3" | Marshall 1 | 10 |
| "activate performance 4" | Marshall 2 | 15 |
| "activate performance 5" | Mesa 1 | 20 |
| "activate performance 6" | Mesa 2 | 25 |
| "activate performance 7" | California Tweed | 30 |
| "activate performance 8" | High Gain 1 | 35 |
| "activate performance 9" | High Gain 2 | 40 |
| "activate performance 10" | Deluxe Reverb | 45 |
| "activate performance 11–20" | *(unnamed)* | 50–95 |

PC formula: `(performance# - 1) × 5`. No bank select needed for performances 1–25.

To add an alias for a performance, add an entry alongside the numbered one in `config.json`:
```json
"performance 11": { "pc": 50 },
"My Patch Name": { "pc": 50 }
```

**Pro Tools transport (requires HUI setup):**

| Say | Action |
|-----|--------|
| "Hey Google, activate play" | Spacebar (play/stop toggle) |
| "Hey Google, activate stop" | Spacebar |
| "Hey Google, activate go to beginning" | Return key |
| "Hey Google, activate start recording" | Cmd+Spacebar |
| "Hey Google, activate stop recording" | Spacebar |

---

## Managing Commands

Edit `config.json` to add, remove, or change commands. Three command types are supported:

```json
"commands": {
  "my CC command":  { "cc": 20, "value": 64 },
  "my PC command":  { "pc": 10 },
  "my multi command": { "multi": [
    { "cc": 17, "value": 0 },
    { "cc": 18, "value": 0 }
  ]},
  "play": { "os": "play" }
}
```

**Reload config without restarting:**
```bash
kill -HUP $(lsof -ti :3000)
```

**After adding or renaming commands**, tell Google to sync:
> "Hey Google, sync my devices"

---

## Logs

```bash
# Live log
tail -f ~/Library/Logs/midi-voice-command.log

# Cloudflare tunnel log (system daemon via sudo install)
tail -f /Library/Logs/com.cloudflare.cloudflared.err.log

# Cloudflare tunnel log (user LaunchAgent)
tail -f ~/Library/Logs/cloudflared.log
```

---

## Manual server control

```bash
# Start
launchctl load ~/Library/LaunchAgents/com.collinpeterson.midi-voice-command.plist

# Stop
launchctl unload ~/Library/LaunchAgents/com.collinpeterson.midi-voice-command.plist

# Restart
launchctl unload ~/Library/LaunchAgents/com.collinpeterson.midi-voice-command.plist && \
launchctl load ~/Library/LaunchAgents/com.collinpeterson.midi-voice-command.plist
```

---

## cloudflared as a user LaunchAgent

Use this approach instead of `sudo cloudflared service install` if you don't have Homebrew or can't run sudo.

**1. Download cloudflared manually:**
```bash
# Get the latest release URL from https://github.com/cloudflare/cloudflared/releases
# For Apple Silicon:
curl -L https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-darwin-arm64.tgz -o /tmp/cloudflared.tgz
tar -xzf /tmp/cloudflared.tgz -C /tmp
mkdir -p ~/.local/bin
mv /tmp/cloudflared ~/.local/bin/cloudflared
chmod +x ~/.local/bin/cloudflared
```

**2. Create a LaunchAgent plist** at `~/Library/LaunchAgents/com.yourname.cloudflared.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.yourname.cloudflared</string>
    <key>ProgramArguments</key>
    <array>
        <string>/path/to/cloudflared</string>
        <string>tunnel</string>
        <string>--no-autoupdate</string>
        <string>run</string>
        <string>--token</string>
        <string>YOUR_TUNNEL_TOKEN</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardOutPath</key>
    <string>/Users/yourusername/Library/Logs/cloudflared.log</string>
    <key>StandardErrorPath</key>
    <string>/Users/yourusername/Library/Logs/cloudflared.log</string>
</dict>
</plist>
```

Get your tunnel token from the Zero Trust dashboard: Networks → Tunnels → your tunnel → Configure → click the token to copy it.

**3. Load it:**
```bash
launchctl load ~/Library/LaunchAgents/com.yourname.cloudflared.plist
```

**Restart cloudflared:**
```bash
launchctl kickstart -k gui/$(id -u)/com.yourname.cloudflared
```

---

## Troubleshooting

**Server won't start — MIDI device not found**
Run the device lister command, check the exact name, and update `midiDevice` in `config.json`.

**"Hey Google, midi-voice isn't available"**
- Check the server is running: `lsof -i :3000`
- Check the Cloudflare tunnel is up: `curl https://midi.yourdomain.com/commands`
- Check logs: `tail -20 ~/Library/Logs/midi-voice-command.log`

**New commands not recognized by Google**
Say "Hey Google, sync my devices" after editing `config.json`.

**Re-link after changing the fulfillment URL**
Unlink MIDI Voice in the Google Home app (Settings → Works with Google) then re-link.

**502 Bad Gateway / "cannot reach MIDI Voice" after moving to a new Mac**
You likely have cloudflared running on two machines simultaneously. Cloudflare load-balances between all active connectors on a tunnel — requests that land on the old machine return 502 if the server isn't running there.

Fix: stop cloudflared on the old Mac:
```bash
launchctl unload ~/Library/LaunchAgents/com.yourname.cloudflared.plist
```
Wait a few seconds for Cloudflare to clear the old connections, then retry.

---

## Future: Marshall JVM 410H Voice Control

PC support is already implemented (see Managing Commands above). A future session will add JVM 410H commands to `config.json` so you can switch amp channels via Google Home.

### Multiple MIDI devices

Currently all CC and PC messages go to the single `midiDevice` defined in `config.json` (the Kemper). The JVM needs to be a **separate MIDI output** since it's a different physical device.

The multi-device pattern already exists in the code (`huiOutput`, `mmcOutput` are separate outputs alongside the main one). The required changes for a future session:

1. Add `"jvmDevice": "your-jvm-midi-interface"` to `config.json`
2. Open a second MIDI output for it at startup
3. Add a `"device"` field to JVM commands in `config.json`
4. Route commands with `"device": "jvm"` to the JVM output in `dispatchCommand`

Example `config.json` commands once implemented:
```json
"clean channel":  { "pc": 0, "device": "jvm" },
"crunch channel": { "pc": 1, "device": "jvm" },
"lead channel":   { "pc": 2, "device": "jvm" }
```

"Hey Google, activate lead channel" → PC 2 → JVM output → JVM 410H switches to programmed lead preset.

> Note: you'll need a MIDI interface connected to the Mac with a 5-pin DIN cable running to the JVM's MIDI IN. The JVM does not have USB MIDI.

---

## Future: Midas M32R Console Control via OSC

The M32R is always on the same network as the Mac running this server. OSC (Open Sound Control) over UDP gives much deeper control than MIDI — any fader, EQ, mute group, scene recall, etc.

### Why OSC over MIDI for the M32R

- Full bidirectional parameter control (faders, EQ, compression, gates, effects, aux sends)
- Scene/snapshot recall
- Mute group toggling
- No extra hardware — M32R and Mac are already on the same network

### Planned implementation

**Dependencies:** add `node-osc` npm package — no other infrastructure needed.

**Config additions:**
```json
{
  "m32rHost": "192.168.1.XX",
  "m32rPort": 10023
}
```

**New `"osc"` command type in `config.json`:**
```json
"tracking setup": { "osc": "scene",     "value": 1 },
"mix setup":      { "osc": "scene",     "value": 2 },
"mute band":      { "osc": "mutegroup", "value": 1 }
```

**Code change:** add OSC dispatch alongside existing MIDI/HUI/MMC in `dispatchCommand`.

### Before the session

Look up the exact OSC address strings for the M32R (X32/M32 OSC protocol is well documented):
- Scene recall address format
- Mute group toggle address format
- Any other commands needed

Default M32R OSC port: **10023 UDP**
