const midi = require('midi');
const express = require('express');
const fs = require('fs');
const path = require('path');

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

process.on('SIGTERM', () => { output.closePort(); process.exit(0); });
process.on('SIGINT',  () => { output.closePort(); process.exit(0); });

// --- Express setup ---

const app = express();
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

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
    token_type: 'Bearer',
    expires_in: 315360000 // 10 years — effectively permanent for personal use
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

function handleExecute(requestId, payload) {
  const results = [];
  for (const command of payload.commands) {
    for (const device of command.devices) {
      const name = commandName(device.id);
      const match = config.commands[name];
      if (match) {
        sendCC(match.cc, match.value);
        results.push({ ids: [device.id], status: 'SUCCESS' });
      } else {
        console.warn(`Unknown command: "${name}"`);
        results.push({ ids: [device.id], status: 'ERROR', errorCode: 'notFound' });
      }
    }
  }
  return { requestId, payload: { commands: results } };
}

function handleQuery(requestId) {
  // Scenes don't have queryable state
  return { requestId, payload: { devices: {} } };
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
    return res.json(handleQuery(requestId));
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

// Manual trigger for testing without Google Home
app.post('/command', (req, res) => {
  const name = (req.body.command || '').toLowerCase().trim();
  const match = config.commands[name];
  if (!match) return res.status(404).json({ error: `Unknown command: "${name}"` });
  sendCC(match.cc, match.value);
  res.json({ ok: true, command: name, cc: match.cc, value: match.value });
});

const PORT = config.port || 3000;
app.listen(PORT, '0.0.0.0', () => {
  console.log(`MIDI Voice Command server listening on port ${PORT}`);
});
