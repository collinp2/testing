const midi = require('midi');
const express = require('express');
const fs = require('fs');
const path = require('path');

const configPath = path.join(__dirname, 'config.json');
let config = JSON.parse(fs.readFileSync(configPath, 'utf8'));

// Reload config on SIGHUP (useful for updating commands without restart)
process.on('SIGHUP', () => {
  config = JSON.parse(fs.readFileSync(configPath, 'utf8'));
  console.log('Config reloaded');
});

// Set up MIDI output
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

// Graceful shutdown
process.on('SIGTERM', () => { output.closePort(); process.exit(0); });
process.on('SIGINT',  () => { output.closePort(); process.exit(0); });

// HTTP server — receives commands from Google Home fulfillment
const app = express();
app.use(express.json());

// POST /command  { "command": "change to channel 1" }
app.post('/command', (req, res) => {
  const command = (req.body.command || '').toLowerCase().trim();
  const match = config.commands[command];
  if (!match) {
    return res.status(404).json({ error: `Unknown command: "${command}"` });
  }
  sendCC(match.cc, match.value);
  res.json({ ok: true, command, cc: match.cc, value: match.value });
});

// GET /devices — list available MIDI ports (useful for setup)
app.get('/devices', (_req, res) => {
  const tmp = new midi.Output();
  const devices = [];
  for (let i = 0; i < tmp.getPortCount(); i++) {
    devices.push({ index: i, name: tmp.getPortName(i) });
  }
  res.json(devices);
});

// GET /commands — list configured commands
app.get('/commands', (_req, res) => {
  res.json(config.commands);
});

const PORT = config.port || 3000;
app.listen(PORT, '0.0.0.0', () => {
  console.log(`MIDI Voice Command server listening on port ${PORT}`);
});
