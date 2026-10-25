// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The pop-up of the ad blocker's toolbar button.

import {addWebUiListener, sendWithPromise} from 'chrome://resources/js/cr.js';

function $(id) {
  return document.getElementById(id);
}

let state = null;

function render(next) {
  state = next;
  const hasSite = !!state.host;
  $('host').textContent = hasSite ? state.host : 'This page';
  $('pageCount').textContent = state.blockedCount.toLocaleString();
  $('hiddenCount').textContent = state.hiddenCount.toLocaleString();
  $('totalCount').textContent = state.totalBlocked.toLocaleString();
  $('globalEnabled').checked = state.enabled;

  const active = state.enabled && hasSite && !state.siteTrusted;
  const power = $('power');
  power.setAttribute('aria-pressed', String(active));
  power.disabled = !hasSite || !state.enabled;
  if (!state.enabled) {
    $('status').textContent = 'The ad blocker is off.';
  } else if (!hasSite) {
    $('status').textContent = 'The ad blocker does not work on this page.';
  } else if (state.siteTrusted) {
    $('status').textContent = 'Paused on this site.';
  } else {
    $('status').textContent = 'Blocking on this site.';
  }

  for (const id of ['pick', 'zap']) {
    $(id).disabled = !active;
  }
  $('reload').disabled = !state.canReload;

  $('itemsSection').hidden = state.items.length === 0;
  const list = $('items');
  list.textContent = '';
  for (const item of state.items.slice().reverse()) {
    const li = document.createElement('li');
    li.title = `${item.url}\n${item.filter}`;
    const text = document.createElement('span');
    text.textContent = item.url.replace(/^https?:\/\//, '');
    const by = document.createElement('span');
    by.className = 'list';
    by.textContent = item.list ? `  — ${item.list}` : '';
    li.append(text, by);
    list.append(li);
  }
}

$('power').addEventListener('click', () => {
  if (!state) {
    return;
  }
  const active = state.enabled && !state.siteTrusted;
  chrome.send('rebaAdblockPopupSetSiteTrusted', [active]);
});

$('globalEnabled').addEventListener('change', (e) => {
  chrome.send('rebaAdblockPopupSetEnabled', [e.target.checked]);
});

$('pick').addEventListener('click', () => {
  chrome.send('rebaAdblockPopupPick', [false]);
});

$('zap').addEventListener('click', () => {
  chrome.send('rebaAdblockPopupPick', [true]);
});

$('reload').addEventListener('click', () => {
  chrome.send('rebaAdblockPopupReload');
});

$('settings').addEventListener('click', () => {
  chrome.send('rebaAdblockPopupOpenSettings', ['']);
});

$('log').addEventListener('click', () => {
  chrome.send('rebaAdblockPopupOpenSettings', ['log']);
});

addWebUiListener('reba-adblock-popup-state', render);
sendWithPromise('rebaAdblockPopupGetState').then(render);
