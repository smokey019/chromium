// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The page that replaces a website the ad blocker blocks. The address of the
// blocked page and what blocked it are in the query string.

import '/strings.m.js';

import {sendWithPromise} from 'chrome://resources/js/cr.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';

const params = new URLSearchParams(location.search);
const blockedUrl = params.get('url') || '';
const filter = params.get('filter') || '';
const list = params.get('list') || '';
const listId = params.get('listid') || '';

function $(id) {
  return document.getElementById(id);
}

function hostOf(url) {
  try {
    return new URL(url).hostname;
  } catch (e) {
    return '';
  }
}

const host = hostOf(blockedUrl);
const blockedByUser = listId === 'user-filters';
// Incognito and Guest windows don't save choices; they last until the browser
// closes.
const offTheRecord = loadTimeData.getBoolean('offTheRecord');

document.title = host ? `${host} is blocked` : 'Website blocked';
$('title').textContent = host ? `${host} is blocked` : 'This website is blocked';
$('reason').textContent = list ?
    `This website is blocked by ${blockedByUser ? 'your own filters' :
                                                  `the list "${list}"`}.` :
    'This website is blocked by the ad blocker.';
$('url').textContent = blockedUrl;
if (list || filter) {
  $('details').hidden = false;
  $('list').textContent = list || 'unknown';
  $('filter').textContent = filter || 'unknown';
}
if (blockedByUser) {
  // It's already in My filters.
  $('block').hidden = true;
}
if (offTheRecord) {
  // "Allow always" would be the same as "Allow for this session".
  $('always').hidden = true;
  $('block').textContent = 'Block for this session';
}

function setBusy(busy) {
  for (const button of document.querySelectorAll('button')) {
    button.disabled = busy;
  }
}

function status(text) {
  $('status').hidden = !text;
  $('status').textContent = text;
}

const FAILED =
    'That did not work. Try again, or change it in Settings > Ad blocker.';

async function allow(action, description) {
  setBusy(true);
  status(description);
  try {
    await sendWithPromise('rebaAdblockBlockedPageAction', action, blockedUrl);
  } catch (e) {
    setBusy(false);
    status(FAILED);
    return;
  }
  // The decision is in effect; go to the page, replacing this one.
  location.replace(blockedUrl);
}

$('back').addEventListener('click', () => {
  if (history.length > 1) {
    history.back();
  } else {
    location.replace('chrome://newtab/');
  }
});

$('session').addEventListener('click', () => {
  allow('allowSession', 'Opening the website for this session...');
});

$('always').addEventListener('click', () => {
  allow('allowAlways', 'Always allowing this website...');
});

$('block').addEventListener('click', async () => {
  setBusy(true);
  try {
    await sendWithPromise(
        'rebaAdblockBlockedPageAction', 'blockAlways', blockedUrl);
  } catch (e) {
    setBusy(false);
    status(FAILED);
    return;
  }
  setBusy(false);
  $('block').hidden = true;
  status(
      offTheRecord ? 'This website stays blocked until you close the browser.' :
                     'Added to My filters. This website stays blocked.');
});
