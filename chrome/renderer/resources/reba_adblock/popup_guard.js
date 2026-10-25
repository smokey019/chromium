// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Stops "click hijacking": a page that opens an ad in a new tab when you click
// anywhere on it, even on an unrelated link. Filter lists only know the ads
// they have seen, and ad networks change addresses all the time, so this looks
// at what the click was on instead.
//
// It runs in the page's own world before any script of the page (see
// CosmeticObserver::RunScriptsAtDocumentStart), watches real clicks, and
// refuses a new window opened by script (window.open(), a script clicking a
// link with target=_blank, a script submitting a form into a new window) when
// the click that allowed it was
//   * on plain page area (not a link, button or anything that looks clickable;
//     a video, or a layer that covers most of the window, counts as plain
//     area),
//   * on a link whose own address is somewhere else than the new window, or
//   * heard by a capture-phase listener on the window or document, which gets
//     every click whatever it was on (that is how ad scripts listen).
// A button or a link that points at the window it opens is left alone, so
// "sign in" pop-ups, share dialogs and payment windows keep working, and so do
// new windows on the same site.
//
// A refused window is reported through an attribute and an event of a random
// name, which the isolated world of the ad blocker listens for. The name
// replaces the placeholder below for each document.
(function() {
'use strict';

const EVENT_NAME = '__REBA_EVENT__';

const win = window;
const doc = document;
const URLCtor = win.URL;
const nowOf = win.performance.now.bind(win.performance);
const computedStyleOf = win.getComputedStyle.bind(win);
const dispatch = win.dispatchEvent.bind(win);
const EventCtor = win.Event;
const apply = Reflect.apply;
const NativeProxy = Proxy;

// How long after a click it still explains a window. User activation lasts
// about five seconds.
const MAX_CLICK_AGE_MS = 6000;

// Things whose click is about something else than opening a window. A video is
// not here: clicking the player is the cover that ad scripts like best.
const CONTROL_TAGS = new Set([
  'button', 'input', 'select', 'textarea', 'option', 'label', 'summary',
  'details',
]);

const CLICK_TYPES = new Set([
  'pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click', 'auxclick',
  'touchstart', 'touchend',
]);
const CONTROL_ROLES = new Set([
  'button', 'link', 'menuitem', 'menuitemcheckbox', 'menuitemradio', 'tab',
  'option', 'checkbox', 'radio', 'switch', 'combobox', 'textbox', 'treeitem',
]);

// --- Where the user last clicked ------------------------------------------

let last = null;

function record(event) {
  if (!event.isTrusted) {
    return;
  }
  let target = event.target;
  try {
    const path = event.composedPath();
    if (path && path.length) {
      target = path[0];
    }
  } catch (e) {
    // Keep event.target.
  }
  // Looked at now, before any script of the page has reacted to the click.
  last = {target, time: nowOf(), found: analyse(target)};
}

for (const type of CLICK_TYPES) {
  win.addEventListener(type, record, {capture: true, passive: true});
}

// --- What a click was on ----------------------------------------------------

// An element that fills much of the window is a layer put over the page, not
// a button, whatever its cursor or onclick say.
function coversMostOfWindow(element) {
  try {
    const rect = element.getBoundingClientRect();
    return rect.width * rect.height >=
        0.5 * win.innerWidth * win.innerHeight;
  } catch (e) {
    return false;
  }
}

// {link: <a>} when the click was in a link, {control: true} when it was on
// something made to be clicked, null for plain page area.
function analyse(target) {
  let node = target;
  for (let depth = 0; node && depth < 25; depth++) {
    if (node.nodeType === 1) {
      const tag = node.localName;
      if (tag === 'a' || tag === 'area') {
        return {link: node};
      }
      if (CONTROL_TAGS.has(tag)) {
        return {control: true};
      }
      const role = node.getAttribute('role');
      if (role && CONTROL_ROLES.has(role)) {
        return {control: true};
      }
      if ((node.hasAttribute('onclick') || node.isContentEditable) &&
          !coversMostOfWindow(node)) {
        return {control: true};
      }
    }
    node = node.parentElement ||
        (node.getRootNode && node.getRootNode().host) || null;
  }
  if (target && target.nodeType === 1) {
    try {
      if (computedStyleOf(target).cursor === 'pointer' &&
          !coversMostOfWindow(target)) {
        return {control: true};
      }
    } catch (e) {
      // Treat it as plain area.
    }
  }
  return null;
}

// --- Addresses ------------------------------------------------------------

const SECOND_LEVEL = new Set(['co', 'com', 'org', 'net', 'gov', 'edu', 'ac']);

// The part of a host name that a site owns: example.com for www.example.com,
// example.co.uk for www.example.co.uk. Not the public suffix list, but close
// enough to tell an ad network from the page it is on.
function siteOf(host) {
  if (/^[\d.]+$/.test(host) || host.indexOf(':') >= 0) {
    return host;
  }
  const parts = host.split('.');
  const n = parts.length;
  if (n <= 2) {
    return host;
  }
  if (parts[n - 1].length === 2 && SECOND_LEVEL.has(parts[n - 2])) {
    return parts.slice(-3).join('.');
  }
  return parts.slice(-2).join('.');
}

const ownSite = siteOf(win.location.hostname);

function isWeb(url) {
  return url.protocol === 'http:' || url.protocol === 'https:';
}

function sameDestination(a, b) {
  return a.origin === b.origin && a.pathname === b.pathname &&
      a.search === b.search;
}

function parse(text) {
  try {
    return new URLCtor(text, doc.baseURI);
  } catch (e) {
    return null;
  }
}

// --- The decision -----------------------------------------------------------

function isNewWindowTarget(name) {
  return name !== '_self' && name !== '_top' && name !== '_parent';
}

// The click event that is being handled right now, if there is one.
function currentEvent() {
  try {
    const event = win.event;
    return event && event.isTrusted ? event : null;
  } catch (e) {
    return null;
  }
}

// Whether the code asking for a window runs from a listener that sits on the
// window or document itself and gets the click before the page does (capture
// phase). That is where ad scripts listen, so that they hear every click on
// the page whatever it is on. A page's own code opens windows from the handler
// of what was clicked; frameworks that listen high up do it while the event
// bubbles.
function inGlobalCaptureListener() {
  const event = currentEvent();
  if (!event || !CLICK_TYPES.has(event.type) ||
      event.eventPhase !== 1 /* Event.CAPTURING_PHASE */) {
    return false;
  }
  const at = event.currentTarget;
  return at === win || at === doc || at === doc.documentElement ||
      at === doc.body;
}

// When a window was last refused. Ad scripts try again from a timer or a
// promise (another way to open it, or after a delay), still on the same click.
let lastRefusalTime = -Infinity;
const RETRY_WINDOW_MS = 1500;

// Whether the new window that script wants at `rawUrl` is a hijack.
function shouldRefuse(rawUrl, targetName) {
  const refuse = decide(rawUrl, targetName);
  if (refuse) {
    lastRefusalTime = nowOf();
  }
  return refuse;
}

function decide(rawUrl, targetName) {
  if (targetName !== undefined && targetName !== null &&
      !isNewWindowTarget(String(targetName))) {
    return false;
  }
  if (!last || nowOf() - last.time > MAX_CLICK_AGE_MS) {
    return false;
  }
  try {
    // Without a click's permission Chrome's own pop-up blocker is in charge.
    if (!win.navigator.userActivation.isActive) {
      return false;
    }
  } catch (e) {
    // Older engine; go on.
  }

  const text = rawUrl === undefined || rawUrl === null ? '' : String(rawUrl);
  let destination = null;
  if (text !== '') {
    destination = parse(text);
    if (!destination) {
      return false;
    }
    if (isWeb(destination)) {
      if (siteOf(destination.hostname) === ownSite) {
        return false;
      }
    } else if (destination.protocol !== 'about:') {
      // mailto:, tel:, blob: and so on are not ad pages.
      return false;
    }
  }
  // An empty or about:blank window is cross-site until proven otherwise: the
  // classic trick opens it and sends it to the ad afterwards.

  const found = last.found;
  if (inGlobalCaptureListener()) {
    // Not an answer to what was clicked, unless it was a link to this address.
    if (found && found.link && destination) {
      const linkUrl = parse(found.link.getAttribute('href') || '');
      if (linkUrl && isWeb(linkUrl) && sameDestination(linkUrl, destination)) {
        return false;
      }
    }
    return true;
  }
  if (!currentEvent() && nowOf() - lastRefusalTime < RETRY_WINDOW_MS) {
    // Outside of any event handler, right after a window was refused: the
    // same script trying another way.
    return true;
  }
  if (found && found.control) {
    return false;
  }
  if (found && found.link) {
    const href = found.link.getAttribute('href');
    if (href === null || href === '' || href.charAt(0) === '#' ||
        /^\s*javascript:/i.test(href)) {
      // A link used as a button.
      return false;
    }
    const linkUrl = parse(href);
    if (!linkUrl || !isWeb(linkUrl)) {
      return false;
    }
    if (destination && sameDestination(linkUrl, destination)) {
      return false;
    }
    return true;
  }
  return true;
}

function refuse(url, how) {
  try {
    const text = url === undefined || url === null ? '' : String(url);
    doc.documentElement.setAttribute(
        EVENT_NAME, JSON.stringify({url: text.slice(0, 2000), how}));
    dispatch(new EventCtor(EVENT_NAME));
  } catch (e) {
    // The report is only for the log.
  }
}

// --- Hooks --------------------------------------------------------------------

function hookMethod(owner, name, handler) {
  const desc = Object.getOwnPropertyDescriptor(owner, name);
  if (!desc || typeof desc.value !== 'function') {
    return;
  }
  desc.value = new NativeProxy(desc.value, {apply: handler});
  Object.defineProperty(owner, name, desc);
}

// window.open(), also in same-origin frames the page makes, which would
// otherwise give a script an unhooked copy.
const hookedWindows = new WeakSet();
function hookWindow(target) {
  if (hookedWindows.has(target)) {
    return;
  }
  hookedWindows.add(target);
  hookMethod(target, 'open', (original, thisArg, args) => {
    if (shouldRefuse(args[0], args[1])) {
      refuse(args[0], 'open');
      return null;
    }
    return apply(original, thisArg, args);
  });
}
hookWindow(win);

// A script clicking a link into a new window.
function refusesLink(anchor) {
  const target = anchor.target;
  return !!target && isNewWindowTarget(target) &&
      !anchor.hasAttribute('download') && shouldRefuse(anchor.href, target);
}

hookMethod(win.HTMLElement.prototype, 'click', (original, thisArg, args) => {
  if (thisArg instanceof win.HTMLAnchorElement && refusesLink(thisArg)) {
    refuse(thisArg.href, 'click');
    return undefined;
  }
  return apply(original, thisArg, args);
});

hookMethod(win.EventTarget.prototype, 'dispatchEvent',
           (original, thisArg, args) => {
             const event = args[0];
             if (event && event.type === 'click' && !event.isTrusted &&
                 thisArg instanceof win.HTMLAnchorElement &&
                 refusesLink(thisArg)) {
               refuse(thisArg.href, 'dispatch');
               return false;
             }
             return apply(original, thisArg, args);
           });

for (const name of ['submit', 'requestSubmit']) {
  hookMethod(win.HTMLFormElement.prototype, name, (original, thisArg, args) => {
    const target = thisArg.target;
    if (target && isNewWindowTarget(target) &&
        shouldRefuse(thisArg.action, target)) {
      refuse(thisArg.action, 'form');
      return undefined;
    }
    return apply(original, thisArg, args);
  });
}

// A same-origin frame made by the page has its own window.open.
const frameWindow =
    Object.getOwnPropertyDescriptor(win.HTMLIFrameElement.prototype,
                                    'contentWindow');
if (frameWindow && frameWindow.get) {
  frameWindow.get = new NativeProxy(frameWindow.get, {
    apply(original, thisArg, args) {
      const result = apply(original, thisArg, args);
      try {
        if (result && result !== win) {
          hookWindow(result);
        }
      } catch (e) {
        // Cross-origin.
      }
      return result;
    },
  });
  Object.defineProperty(win.HTMLIFrameElement.prototype, 'contentWindow',
                        frameWindow);
}
})();
