const midi = require('midi');
const express = require('express');
const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');

const configPath = path.join(__dirname, 'config.json');
let config = JSON.parse(fs.readFileSync(configPath, 'utf8'));

process.on('SIGHUP', () => {
  config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  console.log('Config reloaded');
});

// --- MIDI setup ---

const output = new midi.Output();

function findMidiPort(deviceName) {
  for (let i = 0; i < output.getPortCount(); i++) {
    if (output.getPortName(i).includes(deviceName)) return i;
  }
  return -1;
}

function listMidiDevices() {
  const devices = [];
  for (let i = 0; i < output.getPortCount(); i++) {
    devices.push({ index: i, name: output.getPortName(i) });
  }
  return devices;
}

if (!config.midiDevice) {
  console.error('No midiDevice set in config.json. Available devices:');
  listMidiDevices().forEach(d => console.log(`  ${d.index}: ${d.name}`));
  process.exit(1);
}

const portIndex = findMidiPort(config.midiDevice);
if (portIndex === -1) {
  console.error(`MIDI device "${config.midiDevice}" not found. Available devices:`);
  listMidiDevices().forEach(d => console.log(`  ${d.index}: ${d.name}`));
  process.exit(1);
}

output.openPort(portIndex);
console.log(`Connected to MIDI device: ${output.getPortName(portIndex)}`);

function sendCC(cc, value) {
  const channel = (config.midiChannel || 1) - 1;
  output.sendMessage([0xB0 + channel, cc, value]);
  console.log(`Sent CC ${cc} value ${value} on channel ${channel + 1}`);
}

// --- HUI setup (Pro Tools transport control) ---

let huiOutput = null;
let huiInput = null;
let mmcOutput = null;

function findOutputPort(out, name) {
  for (let i = 0; i < out.getPortCount(); i++) {
    if (out.getPortName(i).includes(name)) return i;
  }
  return -1;
}

function findInputPort(inp, name) {
  for (let i = 0; i < inp.getPortCount(); i++) {
    if (inp.getPortName(i).includes(name)) return i;
  }
  return -1;
}

function initHUI() {
  if (!config.huiOutputDevice || !config.huiInputDevice) return;

  huiOutput = new midi.Output();
  const outIdx = findOutputPort(huiOutput, config.huiOutputDevice);
  if (outIdx === -1) {
    console.error(`HUI output device "${config.huiOutputDevice}" not found`);
    huiOutput = null;
    return;
  }
  huiOutput.openPort(outIdx);
  console.log(`HUI output connected: ${huiOutput.getPortName(outIdx)}`);

  huiInput = new midi.Input();
  huiInput.ignoreTypes(false, true, true); // enable SysEx, ignore timing/activeSensing
  huiInput.on('message', (_deltaTime, message) => {
    if (message[0] === 0xF0) {
      // SysEx ping — echo back
      if (message[1] === 0x00 && message[2] === 0x00 &&
          message[3] === 0x66 && message[4] === 0x05) {
        if (huiOutput) huiOutput.sendMessage(Array.from(message));
      }
    } else if (message[0] === 0x80 && message[1] === 0x00 && message[2] === 0x40) {
      // HUI keepalive (Note Off 0x00 0x40) — respond with Note On to confirm online
      if (huiOutput) huiOutput.sendMessage([0x90, 0x00, 0x00]);
    }
  });
  const inIdx = findInputPort(huiInput, config.huiInputDevice);
  if (inIdx === -1) {
    console.error(`HUI input device "${config.huiInputDevice}" not found`);
    huiInput = null;
    return;
  }
  huiInput.openPort(inIdx);
  console.log(`HUI input connected: ${huiInput.getPortName(inIdx)}`);

  // Announce ourselves to Pro Tools (Note On = online, SysEx ping)
  huiOutput.sendMessage([0x90, 0x00, 0x00]);
  huiOutput.sendMessage([0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x00, 0xF7]);
  console.log('HUI: sent initial online announcement + ping to Pro Tools');
}

// HUI button press/release: CC 12 = zone select, CC 44 = port (|0x40 for press)
// Both messages sent synchronously so nothing can be interleaved between them
function sendHUIButton(zone, port, pressed) {
  const msg1 = [0xB0, 12, zone];
  const msg2 = [0xB0, 44, pressed ? (port | 0x40) : port];
  console.log(`HUI bytes: ${msg1.map(b => b.toString(16).padStart(2,'0')).join(' ')} | ${msg2.map(b => b.toString(16).padStart(2,'0')).join(' ')}`);
  huiOutput.sendMessage(msg1);
  huiOutput.sendMessage(msg2);
}

// --- OS transport (AppleScript keystrokes to Pro Tools) ---

function sendOSTransport(action) {
  let args;
  switch (action) {
    case 'play':
    case 'stop':
      // Activate Pro Tools, then send spacebar (play/stop toggle)
      args = [
        '-e', 'tell application "Pro Tools" to activate',
        '-e', 'delay 0.2',
        '-e', 'tell application "System Events" to tell process "Pro Tools" to key code 49'
      ];
      break;
    case 'record_start':
      // Activate Pro Tools, then send Cmd+spacebar (record + play)
      args = [
        '-e', 'tell application "Pro Tools" to activate',
        '-e', 'delay 0.2',
        '-e', 'tell application "System Events" to tell process "Pro Tools" to key code 49 using command down'
      ];
      break;
    default:
      console.warn(`Unknown OS transport action: ${action}`);
      return;
  }
  execFile('osascript', args, (err) => {
    if (err) console.error(`OS transport error: ${err.message}`);
    else console.log(`OS transport: ${action}`);
  });
}

// --- MMC (MIDI Machine Control) transport ---
// Sent via the same huiOutput (Bus 3). Requires Pro Tools MMC slave enabled on Bus 3.

function sendMMCTransport(action) {
  const out = mmcOutput || huiOutput;
  if (!out) {
    console.warn('MMC output not connected');
    return;
  }
  switch (action) {
    case 'play':
      out.sendMessage([0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7]);
      console.log('MMC: Play');
      break;
    case 'stop':
      out.sendMessage([0xF0, 0x7F, 0x7F, 0x06, 0x01, 0xF7]);
      console.log('MMC: Stop');
      break;
    case 'record_start':
      // Record Strobe (arm) then Play
      out.sendMessage([0xF0, 0x7F, 0x7F, 0x06, 0x06, 0xF7]);
      setTimeout(() => out.sendMessage([0xF0, 0x7F, 0x7F, 0x06, 0x02, 0xF7]), 50);
      console.log('MMC: Record Start');
      break;
    default:
      console.warn(`Unknown MMC action: ${action}`);
  }
}

initHUI();

function initMMC() {
  if (!config.mmcOutputDevice) return;
  mmcOutput = new midi.Output();
  const idx = findOutputPort(mmcOutput, config.mmcOutputDevice);
  if (idx === -1) {
    console.error(`MMC output device "${config.mmcOutputDevice}" not found`);
    mmcOutput = null;
    return;
  }
  mmcOutput.openPort(idx);
  console.log(`MMC output connected: ${mmcOutput.getPortName(idx)}`);
}

initMMC();

process.on('SIGTERM', () => {
  output.closePort();
  if (huiOutput) huiOutput.closePort();
  if (huiInput) huiInput.closePort();
  if (mmcOutput) mmcOutput.closePort();
  process.exit(0);
});
process.on('SIGINT', () => {
  output.closePort();
  if (huiOutput) huiOutput.closePort();
  if (huiInput) huiInput.closePort();
  if (mmcOutput) mmcOutput.closePort();
  process.exit(0);
});

// --- Express setup ---

const app = express();
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Log every incoming request and body
app.use((req, _res, next) => {
  const qs = Object.keys(req.query).length ? ' ?' + new URLSearchParams(req.query).toString() : '';
  console.log(`[${new Date().toISOString()}] ${req.method} ${req.path}${qs}`, JSON.stringify(req.body) || '');
  next();
});

// --- OAuth 2.0 (required by Google Smart Home) ---
// Minimal implementation for personal use — no real user accounts needed.

// Step 1: Google redirects user here to authorize
app.get('/oauth', (req, res) => {
  const { redirect_uri, state, client_id } = req.query;
  if (client_id !== config.oauth.clientId) {
    return res.status(401).send('Invalid client_id');
  }
  // Auto-approve and redirect back with an auth code
  const code = 'midi-auth-code';
  res.redirect(`${redirect_uri}?code=${code}&state=${state}`);
});

// Step 2: Google exchanges the auth code for an access token
app.post('/oauth/token', (req, res) => {
  const { client_id, client_secret, grant_type } = req.body;
  if (client_id !== config.oauth.clientId || client_secret !== config.oauth.clientSecret) {
    return res.status(401).json({ error: 'invalid_client' });
  }
  if (grant_type !== 'authorization_code' && grant_type !== 'refresh_token') {
    return res.status(400).json({ error: 'unsupported_grant_type' });
  }
  res.json({
    access_token: config.oauth.accessToken,
    refresh_token: config.oauth.accessToken + '_refresh',
    token_type: 'Bearer',
    expires_in: 315360000
  });
});

// --- Google Smart Home fulfillment ---

function checkAuth(req, res) {
  const auth = req.headers.authorization || '';
  const token = auth.replace('Bearer ', '');
  if (token !== config.oauth.accessToken) {
    res.status(401).json({ error: 'invalid_token' });
    return false;
  }
  return true;
}

function commandId(name) {
  return name.replace(/\s+/g, '_');
}

function commandName(id) {
  return id.replace(/_/g, ' ');
}

function handleSync(requestId) {
  const devices = Object.keys(config.commands).map(name => ({
    id: commandId(name),
    type: 'action.devices.types.SCENE',
    traits: ['action.devices.traits.Scene'],
    name: { name },
    willReportState: false,
    attributes: { sceneReversible: false }
  }));

  return {
    requestId,
    payload: {
      agentUserId: 'midi-user',
      devices
    }
  };
}

function dispatchCommand(match) {
  if (match.os) {
    sendOSTransport(match.os);
  } else if (match.mmc) {
    sendMMCTransport(match.mmc);
  } else {
    sendCC(match.cc, match.value);
  }
}

function handleExecute(requestId, payload) {
  const results = [];
  for (const command of payload.commands) {
    for (const device of command.devices) {
      const name = commandName(device.id);
      const match = config.commands[name];
      if (match) {
        dispatchCommand(match);
        results.push({ ids: [device.id], status: 'SUCCESS' });
      } else {
        console.warn(`Unknown command: "${name}"`);
        results.push({ ids: [device.id], status: 'ERROR', errorCode: 'notFound' });
      }
    }
  }
  return { requestId, payload: { commands: results } };
}

function handleQuery(requestId, payload) {
  const devices = {};
  for (const device of (payload?.devices || [])) {
    devices[device.id] = { online: true };
  }
  return { requestId, payload: { devices } };
}

app.post('/fulfillment', (req, res) => {
  if (!checkAuth(req, res)) return;

  const { requestId, inputs } = req.body;
  const input = inputs[0];

  if (input.intent === 'action.devices.SYNC') {
    return res.json(handleSync(requestId));
  }
  if (input.intent === 'action.devices.EXECUTE') {
    return res.json(handleExecute(requestId, input.payload));
  }
  if (input.intent === 'action.devices.QUERY') {
    return res.json(handleQuery(requestId, input.payload));
  }

  res.status(400).json({ error: 'Unknown intent' });
});

// --- Utility endpoints ---

app.get('/devices', (_req, res) => {
  const tmp = new midi.Output();
  const devices = [];
  for (let i = 0; i < tmp.getPortCount(); i++) {
    devices.push({ index: i, name: tmp.getPortName(i) });
  }
  res.json(devices);
});

app.get('/commands', (_req, res) => {
  res.json(config.commands);
});

// HUI raw test — try arbitrary zone/port to find the right mapping
app.post('/hui-test', (req, res) => {
  const zone = parseInt(req.body.zone, 16);
  const port = parseInt(req.body.port, 16);
  if (!huiOutput) return res.status(503).json({ error: 'HUI not connected' });
  console.log(`HUI test: zone 0x${zone.toString(16)} port 0x${port.toString(16)}`);
  huiOutput.sendMessage([0xB0, 12, zone]);
  huiOutput.sendMessage([0xB0, 44, port | 0x40]);
  setTimeout(() => {
    huiOutput.sendMessage([0xB0, 12, zone]);
    huiOutput.sendMessage([0xB0, 44, port]);
  }, 100);
  res.json({ zone: zone.toString(16), port: port.toString(16) });
});

// Manual trigger for testing without Google Home
app.post('/command', (req, res) => {
  const name = (req.body.command || '').toLowerCase().trim();
  const match = config.commands[name];
  if (!match) return res.status(404).json({ error: `Unknown command: "${name}"` });
  dispatchCommand(match);
  res.json({ ok: true, command: name });
});

const PORT = config.port || 3000;
app.listen(PORT, '0.0.0.0', () => {
  console.log(`MIDI Voice Command server listening on port ${PORT}`);
});
