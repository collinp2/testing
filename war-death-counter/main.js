const { app, Tray, BrowserWindow, nativeImage, Notification } = require('electron');
const https = require('https');
const path = require('path');
const fs = require('fs');

// ---------------------------------------------------------------------------
// Config (optional: add ntfyTopic to get iPhone push notifications)
// ---------------------------------------------------------------------------

const configPath = path.join(__dirname, 'config.json');
let config = {};
try { config = JSON.parse(fs.readFileSync(configPath, 'utf8')); } catch (_) {}

// ---------------------------------------------------------------------------
// Al Jazeera parsing (primary source)
// Numbers live in the server-rendered <meta name="description"> tag —
// no JavaScript execution needed.
// Example: "Preliminary figures are 1,045 dead in Iran, at least 11 in
//           Israel, six US soldiers and nine killed in Gulf states."
// ---------------------------------------------------------------------------

const AJ_URL = 'https://www.aljazeera.com/news/2026/3/1/us-israel-attacks-on-iran-death-toll-and-injuries-live-tracker';

const WORD_NUMBERS = {
  one:1, two:2, three:3, four:4, five:5, six:6, seven:7, eight:8,
  nine:9, ten:10, eleven:11, twelve:12, thirteen:13, fourteen:14,
  fifteen:15, sixteen:16, seventeen:17, eighteen:18, nineteen:19, twenty:20,
};

function wordOrDigit(s) {
  if (!s) return null;
  const n = parseInt(s.replace(/,/g, ''), 10);
  if (!isNaN(n)) return n;
  return WORD_NUMBERS[s.toLowerCase()] || null;
}

function fetchUrl(url) {
  return new Promise((resolve, reject) => {
    https.get(url, { headers: { 'User-Agent': 'Mozilla/5.0 (compatible; WarDeathCounter/1.0)' } }, res => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        return fetchUrl(res.headers.location).then(resolve).catch(reject);
      }
      const chunks = [];
      res.on('data', d => chunks.push(d));
      res.on('end', () => resolve(Buffer.concat(chunks).toString()));
    }).on('error', reject);
  });
}

function parseAJMeta(html) {
  const m = html.match(/<meta[^>]+name=["']description["'][^>]+content=["']([^"']+)["']/i)
         || html.match(/<meta[^>]+content=["']([^"']+)["'][^>]+name=["']description["']/i);
  if (!m) return null;
  const desc = m[1];

  // Iran: "1,045 dead in Iran"
  const iranM = desc.match(/([\d,]+)\s+dead in Iran/i);
  // Israel: "at least 11 in Israel" or "11 in Israel"
  const israelM = desc.match(/(?:at least\s+)?([\d,]+|[a-z]+)\s+in Israel/i);
  // US: "six US soldiers" or "6 US soldiers" or "6 US military"
  const usM = desc.match(/([\d,]+|[a-z]+)\s+US\s+(?:soldiers?|military|service members?)/i);
  // Gulf states: "nine killed in Gulf states"
  const gulfM = desc.match(/([\d,]+|[a-z]+)\s+killed in Gulf states/i);

  return {
    iran:   iranM   ? wordOrDigit(iranM[1])   : null,
    israel: israelM ? wordOrDigit(israelM[1]) : null,
    us:     usM     ? wordOrDigit(usM[1])     : null,
    gulf:   gulfM   ? wordOrDigit(gulfM[1])   : null,
  };
}

// ---------------------------------------------------------------------------
// Wikipedia fallback
// ---------------------------------------------------------------------------

const WIKI_API = 'https://en.wikipedia.org/w/api.php?action=query'
  + '&prop=revisions&rvprop=content&rvslots=main'
  + '&format=json&formatversion=2&redirects=1'
  + '&titles=2026_Iran_conflict';

function parseWiki(wikitext) {
  function extractField(wt, n) {
    const re = new RegExp('\\|\\s*casualties' + n + '\\s*=([\\s\\S]*?)(?=\\n\\s*\\|)', 'i');
    const m = wt.match(re); return m ? m[1] : '';
  }
  function side1Country(cas1, country) {
    const esc = country.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const sec = cas1.match(new RegExp('\\{\\{flagu\\|' + esc + '\\}\\}[\\s\\S]*?(?=\\n\\s*\\*\\s*\\{\\{flagu|$)', 'i'));
    if (!sec) return null;
    const m = sec[0].match(/\*\*\s*([\d,]+)\s*(?:[\w\s]*?\s+)?(?:people\s+)?killed/i);
    return m ? parseInt(m[1].replace(/,/g, ''), 10) : null;
  }
  function side2Iran(cas2) {
    const clean = cas2.replace(/<ref[\s\S]*?<\/ref>/gi, '');
    const m = clean.match(/[≥≧≥]?\s*([\d,]+)\s*killed/i) || clean.match(/at\s+least\s+([\d,]+)\s*killed/i);
    return m ? parseInt(m[1].replace(/,/g, ''), 10) : null;
  }
  const cas1 = extractField(wikitext, 1), cas2 = extractField(wikitext, 2);
  return {
    iran:   side2Iran(cas2),
    israel: side1Country(cas1, 'Israel'),
    us:     side1Country(cas1, 'United States'),
  };
}

async function fetchData() {
  let result = null;

  // Try Al Jazeera first
  try {
    const html = await fetchUrl(AJ_URL);
    result = parseAJMeta(html);
    if (result && (result.iran || result.israel || result.us)) {
      console.log('Source: Al Jazeera', result);
    } else {
      result = null;
    }
  } catch (e) {
    console.error('Al Jazeera fetch failed:', e.message);
  }

  // Fall back to Wikipedia if AJ parsing failed
  if (!result) {
    try {
      const html = await fetchUrl(WIKI_API);
      const data = JSON.parse(html);
      const page = data.query.pages[Object.keys(data.query.pages)[0]];
      result = parseWiki(page.revisions[0].slots.main.content);
      console.log('Source: Wikipedia (fallback)', result);
    } catch (e) {
      console.error('Wikipedia fallback failed:', e.message);
    }
  }

  return {
    iran:    result?.iran    || null,
    israel:  result?.israel  || null,
    us:      result?.us      || null,
    gulf:    result?.gulf    || null,
    updated: new Date().toISOString(),
  };
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------

function sendMacNotification(title, body) {
  if (!Notification.isSupported()) return;
  new Notification({ title, body, silent: false }).show();
}

function sendNtfy(body) {
  if (!config.ntfyTopic) return;
  const url = 'https://ntfy.sh/' + config.ntfyTopic;
  const postData = body;
  const req = https.request(url, {
    method: 'POST',
    headers: {
      'Content-Type': 'text/plain',
      'Title': 'War Death Counter',
      'Priority': 'default',
    },
  });
  req.on('error', e => console.error('NTFY error:', e.message));
  req.write(postData);
  req.end();
}

function notify(changes) {
  if (changes.length === 0) return;
  const body = changes.join('\n');
  sendMacNotification('War Death Counter Updated', body);
  sendNtfy(body);
}

// ---------------------------------------------------------------------------
// Tray label formatting
// ---------------------------------------------------------------------------

function trayLabel(data) {
  if (!data.iran && !data.israel && !data.us) return '⏳';
  const fmt = n => (n || 0).toLocaleString();
  const total = (data.iran || 0) + (data.israel || 0) + (data.us || 0) + (data.gulf || 0);
  return `☠️${fmt(total)}  🇮🇷${fmt(data.iran)}  🇮🇱${fmt(data.israel)}  🇺🇸${fmt(data.us)}  🌍${fmt(data.gulf)}`;
}

// ---------------------------------------------------------------------------
// App
// ---------------------------------------------------------------------------

// 1×1 transparent PNG — invisible icon; tray label carries the display
const BLANK_ICON = 'data:image/png;base64,'
  + 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==';

// ---------------------------------------------------------------------------
// Persist last known values across restarts
// ---------------------------------------------------------------------------

const statePath = path.join(__dirname, 'state.json');

function loadState() {
  try { return JSON.parse(fs.readFileSync(statePath, 'utf8')); } catch (_) { return {}; }
}

function saveState(data) {
  try { fs.writeFileSync(statePath, JSON.stringify(data)); } catch (_) {}
}

let tray, win;
let current = loadState(); // { iran, israel, us } from last run, or {} on first ever run
let firstLoad = Object.keys(current).length === 0; // only skip notifications if no prior state

app.setName('War Death Counter');
app.dock?.hide();
app.setActivationPolicy?.('accessory'); // macOS: don't show in dock or app switcher

// Prevent multiple instances
if (!app.requestSingleInstanceLock()) {
  app.quit();
  process.exit(0);
}

app.whenReady().then(() => {
  const icon = nativeImage.createFromDataURL(BLANK_ICON);
  tray = new Tray(icon);
  tray.setTitle('⏳ loading…');

  win = new BrowserWindow({
    width: 520,
    height: 300,
    show: false,
    frame: false,
    resizable: false,
    skipTaskbar: true,
    webPreferences: { nodeIntegration: false, contextIsolation: true },
  });
  win.loadFile('index.html');
  win.on('blur', () => win.hide());

  tray.on('click', () => {
    if (win.isVisible()) {
      win.hide();
    } else {
      const { x, y } = tray.getBounds();
      const [w, h] = [520, 300];
      win.setPosition(Math.round(x - w / 2 + 8), Math.round(y + 4));
      win.show();
      win.focus();
    }
  });

  fetchAndUpdate();
  setInterval(fetchAndUpdate, 60 * 60 * 1000); // every hour
});

async function fetchAndUpdate() {
  let data;
  try {
    data = await fetchData();
  } catch (e) {
    console.error('Fetch failed:', e.message);
    tray?.setTitle('⚠️ offline');
    return;
  }

  // Detect changes (skip notification on first load)
  const changes = [];
  if (!firstLoad) {
    if (data.iran   !== null && data.iran   !== current.iran)   changes.push(`🇮🇷 Iran: ${(current.iran   || 0).toLocaleString()} → ${data.iran.toLocaleString()}`);
    if (data.israel !== null && data.israel !== current.israel) changes.push(`🇮🇱 Israel: ${(current.israel || 0).toLocaleString()} → ${data.israel.toLocaleString()}`);
    if (data.us     !== null && data.us     !== current.us)     changes.push(`🇺🇸 US: ${(current.us     || 0).toLocaleString()} → ${data.us.toLocaleString()}`);
    if (data.gulf   !== null && data.gulf   !== current.gulf)   changes.push(`🌍 Gulf states: ${(current.gulf || 0).toLocaleString()} → ${data.gulf.toLocaleString()}`);
  }
  firstLoad = false;

  current = { iran: data.iran, israel: data.israel, us: data.us, gulf: data.gulf };
  saveState(current);
  tray.setTitle(trayLabel(current));

  if (win && !win.isDestroyed()) {
    win.webContents.executeJavaScript(`updateData(${JSON.stringify(data)})`).catch(() => {});
  }

  notify(changes);
}

app.on('window-all-closed', e => e.preventDefault()); // stay alive with no windows
