// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Rebrands Chromium's user-visible strings as REBA.
//
// Rewrites the text of the <message> elements in the branded string files (and
// the shared ones with a Chromium branch), so the branding is regenerated after
// an upstream update instead of being merged by hand:
//   - "Chromium" becomes "REBA", except in the phrases in KEEP (copyright
//     holders, the Chromium project credit, ChromeOS names, developer notes);
//   - a few messages get fixed edits (OVERRIDES), such as the formal name
//     "REBA Browser" and the About page's credit line;
//   - attributes (desc=...), comments and translator examples (<ex>) are left
//     alone, so the diff stays small.
// An override whose upstream text changed stops the script, so a reworded
// message gets looked at instead of silently keeping our old wording.
//
// After an update, when these files conflict: take upstream's copy
// (`git checkout --ours -- <file>` during a rebase), then run this script.
//
// node tools/reba/rebrand.mjs          rewrite the files in the working tree
// node tools/reba/rebrand.mjs --check  list what would change, exit 1 if any
// node tools/reba/rebrand.mjs --from <ref>
//                                      rebrand the files as they are at <ref>
//                                      into the working tree
// node tools/reba/rebrand.mjs --verify <upstream ref> <our ref>
//                                      rebrand <upstream ref> in memory and
//                                      compare with <our ref> byte for byte
// Options: --src <dir> (default: the checkout this script is in)
import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export const FILES = [
  'chrome/app/chromium_strings.grd',
  'chrome/app/settings_chromium_strings.grdp',
  'components/components_chromium_strings.grd',
  'extensions/strings/extensions_chromium_strings.grdp',
  'chrome/app/password_manager_ui_strings.grdp',
  'components/autofill_payments_strings.grdp',
  'components/autofill_strings.grdp',
  'components/management_strings.grdp',
  'components/new_or_sad_tab_strings.grdp',
  'components/page_info_strings.grdp',
  'components/password_manager_strings.grdp',
  'components/privacy_sandbox_strings.grd',
  'components/reset_password_strings.grdp',
  'components/search_engine_choice_strings.grdp',
  'components/security_interstitials_strings.grdp',
  'components/ssl_errors_strings.grdp',
];

const NAME = 'REBA';

// Phrases that keep saying Chromium.
const KEEP = [
  'The Chromium Authors',           // copyright holders (except the overrides)
  'Chromium open source project',   // the credit on the About page
  'Chromium developers',
  'ChromiumOS',
  'Chromium OS',
  'Not used in Chromium',           // placeholders nobody sees
];

// Fixed edits of single messages: [from, to] pairs applied to the upstream
// text instead of the general rule. Each `from` must be found.
const OVERRIDES = {
  IDS_PRODUCT_NAME: [['Chromium', 'REBA Browser']],
  IDS_ABOUT_VERSION_COMPANY_NAME: [['The Chromium Authors', 'REBA']],
  IDS_ABOUT_VERSION_COPYRIGHT: [['The Chromium Authors.', 'REBA.']],
  IDS_VERSION_UI_LICENSE: [
    ['Chromium is made possible by the ', 'REBA is built on the open-source '],
    ['</ph> open source project and other', '</ph> project and other'],
  ],
  IDS_VERSION_UI_LICENSE_CHROMIUM: [
    ['Chromium is made possible by the ', 'REBA is built on the open-source '],
    ['</ph> open source project.', '</ph> project.'],
  ],
  IDS_INTERNAL_DEBUG_PAGES_DISABLED_BODY: [
    ['intended for use by Chromium developers,', 'intended for use by developers,'],
  ],
};

// <message ...attributes...>text</message>; attribute values may contain '>'.
const MESSAGE = /(<message\b(?:[^>"]|"[^"]*")*?\bname\s*=\s*"([^"]+)"(?:[^>"]|"[^"]*")*>)([\s\S]*?)(<\/message>)/g;
const KEEP_OR_EX = new RegExp(
    `(<ex>[\\s\\S]*?</ex>)|(${KEEP.map(k => k.replace(/ /g, '\\s+')).join('|')})|Chromium`, 'g');

export function rebrandText(text, file = '') {
  // An overridden message can have several copies in <if> branches (Google
  // Chrome, Chrome for Testing); the edits apply to the copies that have the
  // text. Edits that no copy has (or had, on a second run) are reported.
  const applied = new Set();
  const seen = new Set();
  const result = text.replace(MESSAGE, (all, open, name, body, close) => {
    const edits = OVERRIDES[name];
    if (edits) {
      seen.add(name);
      edits.forEach(([from, to], i) => {
        if (body.includes(from)) {
          body = body.replace(from, to);
          applied.add(`${name}/${i}`);
        } else if (body.includes(to)) {
          applied.add(`${name}/${i}`);
        }
      });
      return open + body + close;
    }
    return open + body.replace(KEEP_OR_EX, (m, ex, keep) => ex || keep || NAME) + close;
  });
  for (const name of seen) {
    OVERRIDES[name].forEach(([from], i) => {
      if (!applied.has(`${name}/${i}`)) {
        throw new Error(`${file}: no copy of ${name} contains "${from}"; ` +
            'check the new upstream text and update OVERRIDES');
      }
    });
  }
  return result;
}

function parseArgs(argv) {
  const args = {
    src: path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..'),
    mode: 'write',
    refs: [],
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--src') args.src = argv[++i];
    else if (a === '--check') args.mode = 'check';
    else if (a === '--from') { args.mode = 'from'; args.refs = [argv[++i]]; }
    else if (a === '--verify') { args.mode = 'verify'; args.refs = [argv[++i], argv[++i]]; }
    else throw new Error(`unknown argument ${a}`);
  }
  return args;
}

function gitShow(src, ref, file) {
  return execFileSync('git', ['show', `${ref}:${file}`], {cwd: src, maxBuffer: 1 << 28}).toString();
}

function changedMessages(before, after) {
  const names = t => new Map([...t.matchAll(MESSAGE)].map(m => [m[2], m[3]]));
  const a = names(before), b = names(after);
  return [...b.keys()].filter(k => a.get(k) !== b.get(k));
}

if (process.argv[1] &&
    path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = parseArgs(process.argv.slice(2));
  let differences = 0;
  for (const file of FILES) {
    const full = path.join(args.src, file);
    if (args.mode === 'verify') {
      const ours = gitShow(args.src, args.refs[1], file);
      const made = rebrandText(gitShow(args.src, args.refs[0], file), file);
      if (made !== ours) {
        differences++;
        const changed = changedMessages(ours, made);
        console.log(`DIFFERS ${file}: ${changed.join(', ') || '(outside messages)'}`);
      } else {
        console.log(`same    ${file}`);
      }
      continue;
    }
    const input = args.mode === 'from' ? gitShow(args.src, args.refs[0], file)
                                        : fs.readFileSync(full, 'utf8');
    const output = rebrandText(input, file);
    const current = fs.readFileSync(full, 'utf8');
    if (output === current) continue;
    differences++;
    const changed = changedMessages(current, output);
    if (args.mode === 'check') {
      console.log(`${file}: ${changed.length} message(s) to rebrand: ${changed.join(', ')}`);
    } else {
      fs.writeFileSync(full, output);
      console.log(`${file}: ${changed.length} message(s) rebranded`);
    }
  }
  if (args.mode === 'verify') {
    console.log(differences ? `${differences} file(s) differ` : 'all files identical');
  } else if (!differences) {
    console.log('nothing to rebrand');
  }
  process.exit(args.mode === 'write' || args.mode === 'from' ? 0 : (differences ? 1 : 0));
}
