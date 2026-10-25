// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The element picker and the zapper. Runs in the isolated world of the ad
// blocker (see cosmetic_filters.js). `window.__rebaStartPicker(zapper)` starts
// it. `__rebaBridge('addFilter', selector)` saves a filter.
//
// Its styles are constructed style sheets and, for the preview, a user style
// sheet the renderer inserts: unlike <style> elements, neither is subject to
// the page's content security policy.
(function() {
'use strict';

// Own property: an element with that id or name would also show up as a
// property of the window.
if (Object.hasOwn(window, '__rebaStartPicker')) return;

const bridge = __rebaBridge;
// The page can shadow document.querySelectorAll with a named element.
const querySelectorAll =
    Function.prototype.call.bind(Document.prototype.querySelectorAll);

let host = null;       // Element holding the UI, in the page.
let root = null;       // Its shadow root.
let box = null;        // The highlight rectangle.
let panel = null;      // Controls shown after an element was picked.
let current = null;    // Element under the pointer.
let picked = null;     // Element the user clicked.
let zapperMode = false;
let previewOn = false;

const COLOR = '#e8453c';

function css() {
  return `
    :host { all: initial; }
    .box { position: fixed; z-index: 2147483646; pointer-events: none;
      border: 2px solid ${COLOR}; background: rgba(232, 69, 60, .18);
      box-sizing: border-box; display: none; }
    .shield { position: fixed; inset: 0; z-index: 2147483645; cursor: crosshair; }
    .panel { position: fixed; right: 16px; bottom: 16px; z-index: 2147483647;
      width: 340px; box-sizing: border-box; padding: 12px;
      font: 13px/1.4 system-ui, sans-serif; color: #222; background: #fff;
      border: 1px solid #bbb; border-radius: 8px;
      box-shadow: 0 6px 24px rgba(0, 0, 0, .3); }
    @media (prefers-color-scheme: dark) {
      .panel { color: #eee; background: #2b2b2b; border-color: #555; }
      textarea { color: #eee; background: #1e1e1e; border-color: #555; }
    }
    .title { font-weight: 600; margin-bottom: 6px; }
    .hint { opacity: .75; margin-bottom: 8px; }
    textarea { width: 100%; box-sizing: border-box; height: 64px; resize: vertical;
      font: 12px ui-monospace, Consolas, monospace; padding: 6px;
      border: 1px solid #bbb; border-radius: 4px; }
    .row { display: flex; gap: 6px; margin-top: 8px; flex-wrap: wrap; }
    button { font: inherit; padding: 5px 12px; border-radius: 4px;
      border: 1px solid #999; background: #f2f2f2; color: #222; cursor: pointer; }
    button.primary { background: ${COLOR}; border-color: ${COLOR}; color: #fff; }
    @media (prefers-color-scheme: dark) {
      button { background: #3a3a3a; color: #eee; border-color: #666; }
      button.primary { background: ${COLOR}; color: #fff; }
    }
    .count { margin-top: 6px; opacity: .75; }
  `;
}

// ---------------------------------------------------------------------------
// Selectors.

function escape(s) {
  return CSS.escape(s);
}

function isUnique(selector, el) {
  try {
    const found = querySelectorAll(document, selector);
    return found.length === 1 && found[0] === el;
  } catch (e) {
    return false;
  }
}

// Class names that look generated (hashes from CSS-in-JS) change between
// visits, so they make poor selectors.
function stableClass(name) {
  if (name.length > 40) return false;
  if (/\d{4,}/.test(name)) return false;
  if (/^[a-z]{1,3}[A-Za-z0-9_-]{7,}$/.test(name) && /[A-Z]/.test(name) && /\d/.test(name)) return false;
  if (/^(css|sc|jss|emotion|styled)-/.test(name)) return false;
  return true;
}

function simpleSelector(el) {
  const tag = el.tagName.toLowerCase();
  if (el.id && isUnique('#' + escape(el.id), el)) return '#' + escape(el.id);

  const classes = Array.from(el.classList).filter(stableClass);
  for (const c of classes) {
    const s = tag + '.' + escape(c);
    if (isUnique(s, el)) return s;
  }
  for (let i = 0; i < classes.length; i++) {
    for (let j = i + 1; j < classes.length; j++) {
      const s = tag + '.' + escape(classes[i]) + '.' + escape(classes[j]);
      if (isUnique(s, el)) return s;
    }
  }
  for (const attr of ['data-testid', 'data-test', 'data-ad', 'data-ad-slot',
                      'aria-label', 'name', 'role', 'title']) {
    const v = el.getAttribute(attr);
    if (v && v.length < 80) {
      const s = tag + '[' + attr + '="' + v.replace(/["\\]/g, '\\$&') + '"]';
      if (isUnique(s, el)) return s;
    }
  }
  for (const attr of ['src', 'href', 'data']) {
    const v = el.getAttribute(attr);
    if (v && /^https?:/.test(v)) {
      try {
        const u = new URL(v, location.href);
        const s = tag + '[' + attr + '^="' + u.origin + u.pathname.replace(/["\\]/g, '\\$&') + '"]';
        if (isUnique(s, el)) return s;
      } catch (e) {
        // Not a URL.
      }
    }
  }
  return null;
}

function selectorFor(el) {
  const simple = simpleSelector(el);
  if (simple) return simple;
  // Build a path up from the element until it's unique.
  const parts = [];
  let node = el;
  while (node && node.nodeType === Node.ELEMENT_NODE && node !== document.documentElement) {
    let part = simpleSelector(node);
    if (!part) {
      part = node.tagName.toLowerCase();
      const parent = node.parentElement;
      if (parent) {
        const same = Array.from(parent.children).filter((c) => c.tagName === node.tagName);
        if (same.length > 1) part += ':nth-of-type(' + (same.indexOf(node) + 1) + ')';
      }
    }
    parts.unshift(part);
    const selector = parts.join(' > ');
    if (isUnique(selector, el)) return selector;
    node = node.parentElement;
  }
  return parts.join(' > ');
}

// ---------------------------------------------------------------------------
// UI.

function elementAt(x, y) {
  // The shield sits above the page; look through it.
  const shield = root.querySelector('.shield');
  shield.style.pointerEvents = 'none';
  let el = document.elementFromPoint(x, y);
  shield.style.pointerEvents = '';
  if (el === host) el = null;
  return el;
}

function place(el) {
  if (!el) {
    box.style.display = 'none';
    return;
  }
  const r = el.getBoundingClientRect();
  box.style.display = 'block';
  box.style.left = r.left + 'px';
  box.style.top = r.top + 'px';
  box.style.width = r.width + 'px';
  box.style.height = r.height + 'px';
}

function stop() {
  window.removeEventListener('keydown', onKey, true);
  removePreview();
  if (host) host.remove();
  host = root = box = panel = current = picked = null;
}

function onKey(event) {
  if (event.key === 'Escape') {
    event.preventDefault();
    event.stopPropagation();
    stop();
  }
}

function onMove(event) {
  if (picked) return;
  current = elementAt(event.clientX, event.clientY);
  place(current);
}

function onClick(event) {
  event.preventDefault();
  event.stopPropagation();
  if (picked) return;
  const el = elementAt(event.clientX, event.clientY);
  if (!el || el === document.documentElement || el === document.body) return;
  if (zapperMode) {
    el.remove();
    place(null);
    bridge('hidden', 1);
    return;
  }
  picked = el;
  place(el);
  showPanel(el);
}

function showPanel(el) {
  const selector = selectorFor(el);
  panel = document.createElement('div');
  panel.className = 'panel';

  const title = document.createElement('div');
  title.className = 'title';
  title.textContent = 'Block element';
  const hint = document.createElement('div');
  hint.className = 'hint';
  hint.textContent = 'Edit the selector, check the preview, then create the filter.';
  const text = document.createElement('textarea');
  text.value = selector;
  text.spellcheck = false;
  const count = document.createElement('div');
  count.className = 'count';

  const row = document.createElement('div');
  row.className = 'row';
  const preview = button('Preview', false, () => togglePreview(text, count));
  const create = button('Create', true, () => {
    const s = text.value.trim();
    if (s) bridge('addFilter', s);
    stop();
  });
  const pickAgain = button('Pick', false, () => {
    removePreview();
    panel.remove();
    panel = null;
    picked = null;
  });
  const cancel = button('Cancel', false, stop);
  row.append(preview, create, pickAgain, cancel);

  text.addEventListener('input', () => updateCount(text, count));
  panel.append(title, hint, text, row, count);
  root.append(panel);
  updateCount(text, count);
}

function button(label, primary, onClick) {
  const b = document.createElement('button');
  b.textContent = label;
  if (primary) b.className = 'primary';
  b.addEventListener('click', (e) => {
    e.stopPropagation();
    onClick();
  });
  return b;
}

function matching(text) {
  try {
    return querySelectorAll(document, text.value.trim()).length;
  } catch (e) {
    return -1;
  }
}

function updateCount(text, count) {
  const n = matching(text);
  count.textContent = n < 0 ? 'Not a valid selector.' :
      n === 1 ? 'Matches 1 element.' : 'Matches ' + n + ' elements.';
  if (previewOn) togglePreview(text, count, true);
}

function removePreview() {
  if (previewOn) {
    bridge('preview', '');
    previewOn = false;
  }
}

function togglePreview(text, count, keepOn) {
  const wasOn = previewOn;
  removePreview();
  if (wasOn && !keepOn) return;
  const s = text.value.trim();
  // A valid selector has no braces, which could end the rule early.
  if (!s || /[{}]/.test(s) || matching(text) < 0) return;
  bridge('preview', s + '{display:none!important}');
  previewOn = true;
}

window.__rebaStartPicker = function(zapper) {
  stop();
  zapperMode = !!zapper;
  host = document.createElement('div');
  host.style.cssText = 'all: initial; position: fixed; z-index: 2147483647;';
  root = host.attachShadow({mode: 'closed'});
  const sheet = new CSSStyleSheet();
  sheet.replaceSync(css());
  root.adoptedStyleSheets = [sheet];
  const shield = document.createElement('div');
  shield.className = 'shield';
  box = document.createElement('div');
  box.className = 'box';
  root.append(shield, box);

  if (zapperMode) {
    const note = document.createElement('div');
    note.className = 'panel';
    const t = document.createElement('div');
    t.className = 'title';
    t.textContent = 'Element zapper';
    const h = document.createElement('div');
    h.className = 'hint';
    h.textContent = 'Click an element to remove it from the page. Press Esc when done.';
    note.append(t, h);
    root.append(note);
  }

  shield.addEventListener('mousemove', onMove, true);
  shield.addEventListener('click', onClick, true);
  shield.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    stop();
  }, true);
  window.addEventListener('keydown', onKey, true);
  document.documentElement.append(host);
};
})();
