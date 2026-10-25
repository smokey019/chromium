// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Runs in an isolated world of every web page frame, next to the page. It does
// what hide rules in a stylesheet can't:
//
//  * runs the procedural cosmetic filters (:has-text(), :upward(), :xpath(),
//    :matches-css(), :style(), :remove(), ...),
//  * asks the browser which generic hide rules (by class and id) apply to the
//    elements that appear; the renderer inserts the rules it gets back.
//
// `__rebaBridge` is installed by the renderer. It is only reachable from this
// world, not from the page.
//
// The page shares the DOM with this world and can shadow DOM properties with
// named elements (`<img name="querySelectorAll">` on the document,
// `<input name="attributes">` in a form), so the DOM is only used through
// functions taken from the prototypes here. Waiting is done with
// requestAnimationFrame() and scheduler.postTask(): unlike setTimeout(), their
// callbacks from this world also run where the site's JavaScript is blocked.
(function() {
'use strict';

// One interpreter per document. document.open() can make the renderer run this
// again for the same document; a window that the next document of the same
// origin reuses keeps this world and the previous interpreter, which stops.
const doc = document;
if (globalThis.__rebaCosmeticDocument === doc) return;
globalThis.__rebaCosmeticDocument = doc;

const bridge = __rebaBridge;
const init = JSON.parse(bridge('init') || '{}');
const procedural = init.procedural || [];
const genericEnabled = !init.generichide;

// ---------------------------------------------------------------------------
// The DOM, out of the page's reach.

const uncurry = (fn) => Function.prototype.call.bind(fn);
const getter = (proto, name) =>
    uncurry(Object.getOwnPropertyDescriptor(proto, name).get);

const docQuerySelectorAll = uncurry(Document.prototype.querySelectorAll);
const docEvaluate = uncurry(Document.prototype.evaluate);
const docDocumentElement = getter(Document.prototype, 'documentElement');
const elQuerySelectorAll = uncurry(Element.prototype.querySelectorAll);
const elClosest = uncurry(Element.prototype.closest);
const elHasAttribute = uncurry(Element.prototype.hasAttribute);
const elSetAttribute = uncurry(Element.prototype.setAttribute);
const elRemoveAttribute = uncurry(Element.prototype.removeAttribute);
const elRemove = uncurry(Element.prototype.remove);
const elAttributes = getter(Element.prototype, 'attributes');
const elId = getter(Element.prototype, 'id');
const elClassList = getter(Element.prototype, 'classList');
const elPreviousSibling = getter(Element.prototype, 'previousElementSibling');
const nodeType = getter(Node.prototype, 'nodeType');
const nodeParent = getter(Node.prototype, 'parentElement');
const nodeText = getter(Node.prototype, 'textContent');
const tokensContain = uncurry(DOMTokenList.prototype.contains);
const tokensRemove = uncurry(DOMTokenList.prototype.remove);
const addListener = uncurry(EventTarget.prototype.addEventListener);
// Names with lone surrogates would spoil the JSON of a whole request.
const isWellFormed = uncurry(String.prototype.isWellFormed);

// ---------------------------------------------------------------------------
// Text patterns: `/regex/flags` (flags as in uBlock Origin) or plain text.

function toRegExp(s) {
  const m = /^\/(.+)\/([imu]*)$/s.exec(s);
  if (!m) return null;
  try {
    return new RegExp(m[1], m[2]);
  } catch (e) {
    return /(?!)/;
  }
}

// Plain text matches anywhere in the string.
function toMatcher(arg) {
  const re = toRegExp(arg);
  if (re) return (s) => re.test(s);
  return (s) => s.includes(arg);
}

// `name: value` or `"name"="value"`, either side a pattern.
function splitPair(arg, separator) {
  let depth = 0, quote = '';
  for (let i = 0; i < arg.length; i++) {
    const c = arg[i];
    if (quote) {
      if (c === quote && arg[i - 1] !== '\\') quote = '';
    } else if (c === '"' || c === "'") {
      quote = c;
    } else if (c === '(' || c === '[') {
      depth++;
    } else if (c === ')' || c === ']') {
      depth--;
    } else if (c === separator && depth === 0) {
      return [arg.slice(0, i), arg.slice(i + 1)];
    }
  }
  return [arg, ''];
}

function unquote(s) {
  s = s.trim();
  if (s.length >= 2 && (s[0] === '"' || s[0] === "'") && s[s.length - 1] === s[0]) {
    return s.slice(1, -1);
  }
  return s;
}

// Plain text matches the whole string, as in uBlock Origin's :matches-attr()
// and :matches-css().
function patternMatcher(s) {
  s = unquote(s);
  const re = toRegExp(s);
  if (re) return (v) => re.test(v);
  // A glob with * wildcards.
  if (s.includes('*')) {
    const glob = new RegExp('^' + s.split('*').map(
        (p) => p.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).join('.*') + '$');
    return (v) => glob.test(v);
  }
  return (v) => v === s;
}

// ---------------------------------------------------------------------------
// Operators. Each takes the current list of elements and returns the next one.

const operators = {
  'has-text': (els, arg) => {
    const matches = toMatcher(arg);
    return els.filter((el) => matches(nodeText(el) || ''));
  },

  'min-text-length': (els, arg) => {
    const n = parseInt(arg, 10) || 0;
    return els.filter((el) => (nodeText(el) || '').length >= n);
  },

  'matches-attr': (els, arg) => {
    const [name, value] = splitPair(arg, '=');
    const nameMatches = patternMatcher(name);
    const valueMatches = value ? patternMatcher(value) : null;
    return els.filter((el) => {
      for (const attr of elAttributes(el)) {
        if (nameMatches(attr.name) && (!valueMatches || valueMatches(attr.value))) {
          return true;
        }
      }
      return false;
    });
  },

  'matches-css': (els, arg, pseudo) => {
    const [name, value] = splitPair(arg, ':');
    const property = name.trim();
    const valueMatches = patternMatcher(value);
    return els.filter((el) => {
      try {
        return valueMatches(getComputedStyle(el, pseudo || null)
                                .getPropertyValue(property));
      } catch (e) {
        return false;
      }
    });
  },

  'matches-css-before': (els, arg) => operators['matches-css'](els, arg, '::before'),
  'matches-css-after': (els, arg) => operators['matches-css'](els, arg, '::after'),

  // Plain text is part of the path and query, as in uBlock Origin, which also
  // percent-encodes what isn't ASCII the way the address is.
  'matches-path': (els, arg) => {
    const matches = toMatcher(
        arg.replace(/\P{ASCII}/gu, (c) => encodeURIComponent(c)));
    return matches(location.pathname + location.search) ? els : [];
  },

  'upward': (els, arg) => {
    const out = new Set();
    const n = /^\d+$/.test(arg.trim()) ? parseInt(arg, 10) : 0;
    for (let el of els) {
      if (n > 0) {
        for (let i = 0; i < n && el; i++) el = nodeParent(el);
        if (el) out.add(el);
      } else {
        try {
          const parent = nodeParent(el);
          const match = parent && elClosest(parent, arg);
          if (match) out.add(match);
        } catch (e) {
          // Invalid selector.
        }
      }
    }
    return Array.from(out);
  },

  'xpath': (els, arg, unused, isFirst) => {
    const out = new Set();
    const roots = isFirst ? [doc] : els;
    for (const root of roots) {
      try {
        const result = docEvaluate(
            doc, arg, root, null, XPathResult.ORDERED_NODE_SNAPSHOT_TYPE, null);
        for (let i = 0; i < result.snapshotLength; i++) {
          const node = result.snapshotItem(i);
          if (nodeType(node) === Node.ELEMENT_NODE) out.add(node);
        }
      } catch (e) {
        // Invalid expression.
      }
    }
    return Array.from(out);
  },
};

function queryDocument(selector) {
  // What follows a leading :matches-path() starts with a combinator.
  selector = selector.trim();
  if (/^[>+~]/.test(selector)) selector = ':root ' + selector;
  try {
    return Array.from(docQuerySelectorAll(doc, selector));
  } catch (e) {
    return [];
  }
}

// CSS that follows an operator, from each element found so far: ` .ad` and
// ` > .ad` look below the element; ` + .ad`, ` ~ .ad` and `.ad` (more
// conditions on the element itself) are asked of its parent, as in uBlock
// Origin.
function queryFrom(elements, selector) {
  const trimmed = selector.trimStart();
  const fromParent =
      /^[+~]/.test(trimmed) || (trimmed === selector && trimmed[0] !== '>');
  const out = new Set();
  for (const el of elements) {
    try {
      let found;
      if (fromParent) {
        const parent = nodeParent(el);
        if (!parent) continue;
        let n = 1;
        for (let s = elPreviousSibling(el); s; s = elPreviousSibling(s)) n++;
        found = elQuerySelectorAll(
            parent, ':scope > :nth-child(' + n + ')' + selector);
      } else {
        found = elQuerySelectorAll(el, ':scope ' + trimmed);
      }
      for (const m of found) out.add(m);
    } catch (e) {
      // Invalid selector.
    }
  }
  return Array.from(out);
}

function runSelector(ops) {
  // null until something narrowed the search down: the whole document.
  let elements = null;
  for (const op of ops) {
    if (op.type === 'css-selector') {
      elements = elements === null ? queryDocument(op.arg) :
                                     queryFrom(elements, op.arg);
    } else if (op.type === 'matches-path' && elements === null) {
      // In front, it only decides whether the rest applies to this page.
      if (operators['matches-path']([doc], op.arg).length === 0) return [];
      continue;
    } else if (operators[op.type]) {
      const isFirst = elements === null;
      if (isFirst && op.type !== 'xpath') {
        elements = Array.from(docQuerySelectorAll(doc, '*'));
      }
      elements = operators[op.type](elements || [], op.arg, undefined, isFirst);
    } else {
      // Not an operator this version knows.
      return [];
    }
    if (elements !== null && elements.length === 0) return [];
  }
  return elements || [];
}

// ---------------------------------------------------------------------------
// Actions.

// Hidden and restyled elements get an attribute that a user style sheet
// matches, as in uBlock Origin. Unlike an inline style, the page can't undo
// that by rewriting `style`, and taking the attribute off again shows an
// element that stopped matching (a recycled list item, say).
const marker = 'x' + Array.from(crypto.getRandomValues(new Uint8Array(6)),
                                (b) => b.toString(16).padStart(2, '0')).join('');

function parseStyle(text) {
  const declarations = [];
  for (const part of text.split(';')) {
    const colon = part.indexOf(':');
    if (colon < 0) continue;
    const name = part.slice(0, colon).trim();
    let value = part.slice(colon + 1).trim();
    let priority = '';
    const important = /\s*!\s*important\s*$/i.exec(value);
    if (important) {
      value = value.slice(0, important.index);
      priority = 'important';
    }
    if (name && value) declarations.push([name, value, priority]);
  }
  return declarations;
}

// The declarations of :style() for a style sheet, or '' if they could break
// out of their rule.
function toDeclarations(text) {
  if (typeof text !== 'string' || /[{}\\]|\/\*/.test(text)) return '';
  return parseStyle(text).map(([name, value, priority]) =>
      name + ':' + value + (priority ? '!important' : '')).join(';');
}

// Actions done to the element itself; they return whether anything changed.
const actions = {
  remove: (el) => {
    elRemove(el);
    return true;
  },
  'remove-attr': (el, arg) => {
    let changed = false;
    for (const name of arg.split('|')) {
      if (elHasAttribute(el, name.trim())) {
        elRemoveAttribute(el, name.trim());
        changed = true;
      }
    }
    return changed;
  },
  'remove-class': (el, arg) => {
    const list = elClassList(el);
    let changed = false;
    for (const name of arg.split('|')) {
      if (tokensContain(list, name.trim())) {
        tokensRemove(list, name.trim());
        changed = true;
      }
    }
    return changed;
  },
};

// Which filters put each marker attribute on an element.
const owners = new WeakMap();

function mark(el, attr, index) {
  let byAttr = owners.get(el);
  if (!byAttr) owners.set(el, byAttr = new Map());
  let indexes = byAttr.get(attr);
  if (!indexes) byAttr.set(attr, indexes = new Set());
  indexes.add(index);
  // Also puts back an attribute the page took off.
  if (!elHasAttribute(el, attr)) elSetAttribute(el, attr, '');
}

function unmark(el, attr, index) {
  const byAttr = owners.get(el);
  const indexes = byAttr && byAttr.get(attr);
  if (!indexes || !indexes.delete(index) || indexes.size) return;
  byAttr.delete(attr);
  elRemoveAttribute(el, attr);
}

const filters = [];
const styleAttrs = new Map();
let styleCss = '';
for (const text of procedural) {
  let parsed;
  try {
    parsed = JSON.parse(text);
  } catch (e) {
    continue;
  }
  if (!parsed || !Array.isArray(parsed.selector)) continue;
  const type = parsed.action ? parsed.action.type : 'hide';
  const arg = parsed.action && parsed.action.arg;
  const filter = {
    selector: parsed.selector,
    index: filters.length,
    attr: '',          // Marker attribute, for hide and style.
    apply: null,       // Otherwise what to do to each element.
    applied: new Set(),  // Elements that have the marker from this filter.
  };
  if (type === 'hide') {
    filter.attr = marker;
  } else if (type === 'style') {
    const declarations = toDeclarations(arg);
    if (!declarations) continue;
    filter.attr = styleAttrs.get(declarations);
    if (!filter.attr) {
      filter.attr = marker + '-' + styleAttrs.size;
      styleAttrs.set(declarations, filter.attr);
      styleCss += '[' + filter.attr + ']{' + declarations + '}\n';
    }
  } else if (actions[type]) {
    filter.apply = (el) => actions[type](el, arg);
  } else {
    continue;
  }
  filters.push(filter);
}

function applyFilter(filter) {
  const found = runSelector(filter.selector);
  let count = 0;
  if (!filter.attr) {
    for (const el of found) {
      try {
        if (filter.apply(el)) count++;
      } catch (e) {
        // Leave the element alone.
      }
    }
    return count;
  }
  const now = new Set(found);
  for (const el of filter.applied) {
    if (!now.has(el)) unmark(el, filter.attr, filter.index);
  }
  for (const el of now) {
    if (!filter.applied.has(el)) count++;
    mark(el, filter.attr, filter.index);
  }
  filter.applied = now;
  return count;
}

// The page changes all the time on busy sites, and a round only gets 40 ms.
// Rounds go on where the last one stopped, so slow filters don't keep the ones
// after them from running, and every filter runs once after each change.
let nextFilter = 0;
let filtersToRun = 0;

function runProcedural() {
  const started = performance.now();
  let count = 0;
  while (filtersToRun > 0) {
    const filter = filters[nextFilter];
    nextFilter = (nextFilter + 1) % filters.length;
    filtersToRun--;
    try {
      count += applyFilter(filter);
    } catch (e) {
      // A broken filter doesn't stop the others.
    }
    if (performance.now() - started > 40) break;
  }
  if (count > 0) bridge('hidden', count);
}

// Attributes the procedural filters look at: only :matches-attr() does (as in
// uBlock Origin, CSS selectors aren't re-checked when an attribute changes).
// null for any attribute, when a name is a pattern.
let filterAttrs = new Set();
for (const filter of filters) {
  for (const op of filter.selector) {
    if (op.type !== 'matches-attr' || filterAttrs === null) continue;
    const name = unquote(splitPair(op.arg, '=')[0]);
    if (/^[A-Za-z_][\w.:-]*$/.test(name)) {
      filterAttrs.add(name);
      filterAttrs.add(name.toLowerCase());
    } else {
      filterAttrs = null;
    }
  }
}

// ---------------------------------------------------------------------------
// Generic hiding by class and id.

// YouTube's live chat gives every message an id of its own; past this many
// names, a document gets no more generic rules.
const kMaxNames = 10000;
const knownClasses = new Set();
const knownIds = new Set();
const newClasses = new Set();
const newIds = new Set();

function note(el) {
  if (knownIds.size < kMaxNames) {
    const id = elId(el);
    if (id && !knownIds.has(id) && isWellFormed(id)) {
      knownIds.add(id);
      newIds.add(id);
    }
  }
  const list = elClassList(el);
  for (let i = 0; i < list.length && knownClasses.size < kMaxNames; i++) {
    const name = list[i];
    if (!knownClasses.has(name) && isWellFormed(name)) {
      knownClasses.add(name);
      newClasses.add(name);
    }
  }
}

function scan(node) {
  if (nodeType(node) !== Node.ELEMENT_NODE) return;
  if (knownIds.size >= kMaxNames && knownClasses.size >= kMaxNames) return;
  note(node);
  for (const el of elQuerySelectorAll(node, '[class],[id]')) note(el);
}

function takeSome(set) {
  const out = [];
  for (const value of set) {
    if (out.length === 1000) break;
    out.push(value);
  }
  for (const value of out) set.delete(value);
  return out;
}

function askGeneric() {
  if (newClasses.size === 0 && newIds.size === 0) return;
  // What doesn't fit goes in the next round.
  const classes = takeSome(newClasses);
  const ids = takeSome(newIds);
  // The renderer inserts the rules that come back.
  if (!bridge('generic', JSON.stringify({classes, ids}))) {
    // Not sent; ask again when the names turn up again.
    for (const name of classes) knownClasses.delete(name);
    for (const id of ids) knownIds.delete(id);
  }
}

// ---------------------------------------------------------------------------
// Watching the page.

let stopped = false;
let roundPending = false;
let lastRound = -Infinity;

function hasWork() {
  return filtersToRun > 0 || newClasses.size > 0 || newIds.size > 0;
}

// A round runs before the next frame is painted, at most every 100 ms. Pages
// that aren't painted (hidden, prerendered) get no animation frames, hence
// the task too.
function schedule() {
  if (roundPending || stopped || !hasWork()) return;
  roundPending = true;
  let ran = false;
  const run = () => {
    if (ran) return;
    ran = true;
    round();
  };
  const wait = Math.max(0, lastRound + 100 - performance.now());
  if (wait === 0) requestAnimationFrame(run);
  scheduler.postTask(run, {delay: wait || 100}).catch(() => {});
}

function round() {
  roundPending = false;
  if (document !== doc) {
    // The window went on to another document, which has its own interpreter.
    stop();
    return;
  }
  lastRound = performance.now();
  try {
    if (filtersToRun > 0) runProcedural();
  } catch (e) {
    // Whatever happened, generic hiding still gets its turn.
  }
  try {
    if (genericEnabled) askGeneric();
  } catch (e) {
    // Try again next round.
  }
  schedule();
}

function onMutations(mutations) {
  try {
    let changed = false;
    for (const mutation of mutations) {
      if (mutation.type === 'childList') {
        changed = true;
        if (genericEnabled) {
          for (const node of mutation.addedNodes) scan(node);
        }
        continue;
      }
      const name = mutation.attributeName;
      if (genericEnabled && (name === 'class' || name === 'id')) {
        note(mutation.target);
      }
      if (filterAttrs === null ? !name.startsWith(marker) :
                                 filterAttrs.has(name)) {
        changed = true;
      }
    }
    if (changed) filtersToRun = filters.length;
  } catch (e) {
    // Let the next change try again.
  }
  schedule();
}

const observer = new MutationObserver(onMutations);

function stop() {
  stopped = true;
  observer.disconnect();
}

function start() {
  if (filters.length) {
    bridge('css', '[' + marker + ']{display:none!important}');
    if (styleCss) bridge('css', styleCss);
  }
  const root = docDocumentElement(doc);
  if (genericEnabled && root) scan(root);
  filtersToRun = filters.length;

  const watchAttrs = genericEnabled || filterAttrs === null || filterAttrs.size > 0;
  const options = {childList: true, subtree: true, attributes: watchAttrs};
  if (watchAttrs && filterAttrs !== null) {
    const names = Array.from(filterAttrs);
    if (genericEnabled) names.push('class', 'id');
    options.attributeFilter = names;
  }
  // The document rather than its element: document.open() replaces that.
  observer.observe(doc, options);
  schedule();

  // :matches-css() can change when the page's style sheets have loaded.
  addListener(window, 'load', () => {
    filtersToRun = filters.length;
    schedule();
  }, {once: true});
}

start();
})();
