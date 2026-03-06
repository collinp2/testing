// War Death Counter — Google Apps Script
// Paste this into Extensions → Apps Script in your Google Sheet.
// Then set up a time-based trigger: Triggers → Add Trigger → updateDeaths → Every hour.

// The article redirects, so we use &redirects=1 to follow it automatically.
const WIKI_API = 'https://en.wikipedia.org/w/api.php?action=query'
  + '&prop=revisions&rvprop=content&rvslots=main'
  + '&format=json&formatversion=2&redirects=1'
  + '&titles=2026_Iran_conflict';

function updateDeaths() {
  const sheet = SpreadsheetApp.getActiveSpreadsheet().getActiveSheet();

  // Preserve existing values as fallback so we never zero out on parse failure
  const fallback = {
    Iran:            numOrNull(sheet.getRange('B1').getValue()),
    Israel:          numOrNull(sheet.getRange('B2').getValue()),
    'United States': numOrNull(sheet.getRange('B3').getValue()),
  };

  let wikitext = '';
  try {
    const resp = UrlFetchApp.fetch(WIKI_API, {
      headers: { 'User-Agent': 'WarDeathCounter/1.0 (Google Apps Script)' },
      muteHttpExceptions: true,
    });
    const data = JSON.parse(resp.getContentText());
    const pages = data.query.pages;
    const page = pages[Object.keys(pages)[0]];
    wikitext = page.revisions[0].slots.main.content;
  } catch (e) {
    Logger.log('Wikipedia fetch failed: ' + e.message);
    sheet.getRange('B4').setValue(new Date().toISOString() + ' (fetch failed)');
    return;
  }

  const results = parseCasualties(wikitext, fallback);

  sheet.getRange('A1').setValue('Iran');
  sheet.getRange('B1').setValue(results.Iran);
  sheet.getRange('A2').setValue('Israel');
  sheet.getRange('B2').setValue(results.Israel);
  sheet.getRange('A3').setValue('United States');
  sheet.getRange('B3').setValue(results['United States']);
  sheet.getRange('A4').setValue('Updated');
  sheet.getRange('B4').setValue(new Date().toISOString());

  Logger.log('Updated: Iran=' + results.Iran
    + ', Israel=' + results.Israel
    + ', US=' + results['United States']);
}

// ---------------------------------------------------------------------------
// Parsing
//
// Infobox structure of 2026_Iran_conflict:
//   casualties1 = plainlist with sub-bullets per country (Israel + US)
//   casualties2 = Iranian side (multiple estimates; we use Red Crescent / lowest)
// ---------------------------------------------------------------------------

function parseCasualties(wikitext, fallback) {
  const cas1 = extractCasualtyField(wikitext, 1);
  const cas2 = extractCasualtyField(wikitext, 2);

  return {
    Iran:            parseSide2Iran(cas2)        || fallback.Iran            || 0,
    Israel:          parseSide1Country(cas1, 'Israel')        || fallback.Israel        || 0,
    'United States': parseSide1Country(cas1, 'United States') || fallback['United States'] || 0,
  };
}

// Extract a casualtiesN field value (everything up to the next | field)
function extractCasualtyField(wikitext, n) {
  const re = new RegExp('\\|\\s*casualties' + n + '\\s*=([\\s\\S]*?)(?=\\n\\s*\\|)', 'i');
  const m = wikitext.match(re);
  return m ? m[1] : '';
}

// Parse killed count for a named country within casualties1.
// The field looks like:
//   * {{flagu|Israel}}:
//   ** 12 people killed
//   * {{flagu|United States}}:
//   ** 6 military personnel killed
function parseSide1Country(cas1, country) {
  // Find the sub-section starting at the country flag template
  const flagKey = country === 'United States' ? 'United States' : country;
  const sectionRe = new RegExp(
    '\\{\\{flagu\\|' + escapeRegex(flagKey) + '\\}\\}[\\s\\S]*?(?=\\n\\s*\\*\\s*\\{\\{flagu|$)',
    'i'
  );
  const section = cas1.match(sectionRe);
  if (!section) return null;

  // Within that section, find the first "N killed" line (** N ... killed)
  const killedRe = /\*\*\s*([\d,]+)\s*(?:[\w\s]*?\s+)?(?:people\s+)?killed/i;
  const m = section[0].match(killedRe);
  return m ? parseInt(m[1].replace(/,/g, ''), 10) : null;
}

// Parse Iranian killed count from casualties2.
// Field has two estimates; we prefer the Red Crescent / lowest figure.
// Looks for: "≥787 killed" or "at least 787 killed" or just the first number before "killed"
function parseSide2Iran(cas2) {
  // Remove refs so numbers inside citations don't interfere
  const clean = cas2.replace(/<ref[^/]*\/>/gi, '').replace(/<ref[\s\S]*?<\/ref>/gi, '');

  // Prefer number after ≥ or "at least" (Red Crescent lower bound)
  const preferred = clean.match(/[≥≧≥]?\s*([\d,]+)\s*killed/i)
                 || clean.match(/at\s+least\s+([\d,]+)\s*killed/i);
  if (preferred) return parseInt(preferred[1].replace(/,/g, ''), 10);

  // Fallback: first number before "killed"
  const any = clean.match(/([\d,]+)\s*(?:[\w\s]*?\s+)?killed/i);
  return any ? parseInt(any[1].replace(/,/g, ''), 10) : null;
}

function escapeRegex(s) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

function numOrNull(v) {
  const n = parseInt(v, 10);
  return isNaN(n) ? null : n;
}
