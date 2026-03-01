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

**MIDI CC commands:**

| Say | MIDI CC sent |
|-----|-------------|
| "Hey Google, activate slot 1" | CC 50 |
| "Hey Google, activate slot 2" | CC 51 |
| "Hey Google, activate slot 3" | CC 52 |
| "Hey Google, activate slot 4" | CC 53 |
| "Hey Google, activate slot 5" | CC 54 |

**Pro Tools transport (requires setup above):**

| Say | Action |
|-----|--------|
| "Hey Google, activate play" | Play |
| "Hey Google, activate stop" | Stop |
| "Hey Google, activate start recording" | Record + Play |
| "Hey Google, activate stop recording" | Stop |

---

## Managing Commands

Edit `config.json` to add, remove, or change commands. Each entry maps a voice command name to a MIDI CC number and value:

```json
"commands": {
  "slot 1": { "cc": 50, "value": 127 },
  "my custom command": { "cc": 20, "value": 64 }
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

# Cloudflare tunnel log
tail -f /Library/Logs/com.cloudflare.cloudflared.err.log
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
