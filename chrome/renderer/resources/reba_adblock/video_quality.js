// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// "Play videos in the best quality". Streaming players choose among the
// qualities listed in the stream's manifest: they start low and move up as
// they measure the connection, and some stay low for minutes. This removes the
// lower qualities from DASH manifests (.mpd) and HLS master playlists (.m3u8)
// as the page gets them, so the player has only the best one this browser can
// decode.
//
// Protected streams often come in groups with their own keys (SD, HD, full HD),
// and the site decides which keys this browser gets. So every DASH adaptation
// set keeps its own best quality, and the player can still fall back to the
// group it can decrypt. HLS variants are kept per audio group and video codec,
// so other languages and codecs stay available.
//
// It runs in the page's own world before any script of the page (see
// CosmeticObserver::RunScriptsAtDocumentStart), in every frame, while the
// setting reba_adblock.best_video_quality is on. Players that fetch their
// manifest in a worker are not reached.
(function() {
'use strict';

const win = window;
const apply = Reflect.apply;
const NativeProxy = Proxy;
const URLCtor = win.URL;
const DOMParserCtor = win.DOMParser;
const XMLSerializerCtor = win.XMLSerializer;
const ResponseCtor = win.Response;
const HeadersCtor = win.Headers;
const TextEncoderCtor = win.TextEncoder;
const TextDecoderCtor = win.TextDecoder;
const defineProperty = Object.defineProperty;
const getOwnPropertyDescriptor = Object.getOwnPropertyDescriptor;
const arrayFrom = Array.from;
const weakMapGet = WeakMap.prototype.get;
const weakMapSet = WeakMap.prototype.set;
const weakMapDelete = WeakMap.prototype.delete;
const location = win.location;
const MediaSourceCtor = win.MediaSource || win.ManagedMediaSource;
const isTypeSupported = MediaSourceCtor && MediaSourceCtor.isTypeSupported ?
    MediaSourceCtor.isTypeSupported.bind(MediaSourceCtor) :
    null;

// Manifests are small. Anything bigger is left alone.
const MAX_MANIFEST_CHARS = 16 * 1024 * 1024;
const MANIFEST_PATH = /\.(mpd|m3u8)$/i;
const MANIFEST_TYPE = /dash\+xml|mpegurl/i;
const VIDEO_CODEC = /^(avc[13]|hvc1|hev1|dvh[1e]|av01|vp0?9|vp8)/i;

function canDecode(mime, codecs) {
  if (!codecs || !isTypeSupported) {
    return true;
  }
  try {
    return isTypeSupported(`${mime || 'video/mp4'}; codecs="${codecs}"`);
  } catch (e) {
    return true;
  }
}

function frameRate(value) {
  if (!value) {
    return 0;
  }
  const parts = String(value).split('/');
  const rate = Number(parts[0]) / (parts.length > 1 ? Number(parts[1]) : 1);
  return isFinite(rate) ? rate : 0;
}

// Higher resolution first, then frame rate (when both say), then bit rate.
function isBetter(a, b) {
  if (a.height !== b.height) {
    return a.height > b.height;
  }
  if (a.fps && b.fps && a.fps !== b.fps) {
    return a.fps > b.fps;
  }
  return a.bandwidth > b.bandwidth;
}

function bestOf(items) {
  let best = null;
  for (const item of items) {
    if (item.decodable && (!best || isBetter(item, best))) {
      best = item;
    }
  }
  return best;
}

// --- DASH ---

function attr(element, name) {
  return element.getAttribute(name) || '';
}

function isVideoSet(set, representations) {
  for (const child of arrayFrom(set.children)) {
    if ((child.localName === 'EssentialProperty' ||
         child.localName === 'SupplementalProperty') &&
        attr(child, 'schemeIdUri').includes('trickmode')) {
      return false;
    }
  }
  if (attr(set, 'contentType') === 'video' ||
      attr(set, 'mimeType').startsWith('video/')) {
    return true;
  }
  return representations.every(r => attr(r, 'mimeType').startsWith('video/'));
}

function pruneDash(text) {
  const doc = new DOMParserCtor().parseFromString(text, 'application/xml');
  const root = doc.documentElement;
  if (!root || root.localName !== 'MPD' ||
      doc.getElementsByTagName('parsererror').length) {
    return null;
  }
  let removed = 0;
  for (const set of arrayFrom(doc.getElementsByTagNameNS('*', 'AdaptationSet'))) {
    const representations = arrayFrom(set.children)
                                .filter(c => c.localName === 'Representation');
    if (representations.length < 2 || !isVideoSet(set, representations) ||
        representations.some(r => r.hasAttribute('dependencyId'))) {
      continue;
    }
    const items = representations.map(r => ({
      element: r,
      height: Number(attr(r, 'height') || attr(set, 'height')) || 0,
      fps: frameRate(attr(r, 'frameRate') || attr(set, 'frameRate')),
      bandwidth: Number(attr(r, 'bandwidth')) || 0,
      decodable: canDecode(attr(r, 'mimeType') || attr(set, 'mimeType'),
                           attr(r, 'codecs') || attr(set, 'codecs')),
    }));
    const best = bestOf(items);
    if (!best) {
      continue;
    }
    for (const item of items) {
      if (item !== best) {
        item.element.remove();
        removed++;
      }
    }
  }
  return removed ? new XMLSerializerCtor().serializeToString(doc) : null;
}

// --- HLS ---

function parseAttributes(list) {
  const attributes = {};
  const pattern = /([A-Z0-9-]+)=("[^"]*"|[^,]*)/g;
  let match;
  while ((match = pattern.exec(list))) {
    attributes[match[1]] = match[2].replace(/^"|"$/g, '');
  }
  return attributes;
}

function prunePlaylist(text) {
  if (!text.includes('#EXT-X-STREAM-INF')) {
    return null;
  }
  const lines = text.split(/\r?\n/);
  const variants = [];
  for (let i = 0; i < lines.length; i++) {
    if (!lines[i].startsWith('#EXT-X-STREAM-INF:')) {
      continue;
    }
    let uri = i + 1;
    while (uri < lines.length && !lines[uri].trim()) {
      uri++;
    }
    if (uri >= lines.length || lines[uri].startsWith('#')) {
      return null;
    }
    const attributes = parseAttributes(lines[i].slice(18));
    const codecs = attributes['CODECS'] || '';
    const videoCodec =
        codecs.split(',').map(c => c.trim()).find(c => VIDEO_CODEC.test(c));
    const resolution = /^(\d+)x(\d+)$/.exec(attributes['RESOLUTION'] || '');
    variants.push({
      first: i,
      last: uri,
      video: !!(videoCodec || resolution),
      group: (attributes['AUDIO'] || '') + '|' +
          (videoCodec ? videoCodec.slice(0, 4).toLowerCase() : ''),
      height: resolution ? Number(resolution[2]) : 0,
      fps: frameRate(attributes['FRAME-RATE']),
      bandwidth: Number(attributes['BANDWIDTH']) || 0,
      decodable: canDecode('video/mp4', codecs),
    });
    i = uri;
  }
  // Audio-only streams (radio) are left alone.
  if (!variants.some(v => v.video)) {
    return null;
  }
  const groups = new Map();
  for (const variant of variants) {
    if (variant.video) {
      if (!groups.has(variant.group)) {
        groups.set(variant.group, []);
      }
      groups.get(variant.group).push(variant);
    }
  }
  const keep = new Set();
  for (const group of groups.values()) {
    const best = bestOf(group);
    for (const variant of best ? [best] : group) {
      keep.add(variant);
    }
  }
  const drop = new Set();
  for (const variant of variants) {
    if (!keep.has(variant)) {
      for (let line = variant.first; line <= variant.last; line++) {
        drop.add(line);
      }
    }
  }
  if (!drop.size) {
    return null;
  }
  return lines.filter((line, index) => !drop.has(index)).join('\n');
}

// Returns the manifest with the lower qualities taken out, or null when `text`
// is not a manifest or has nothing to take out.
function rewrite(text) {
  if (typeof text !== 'string' || text.length > MAX_MANIFEST_CHARS) {
    return null;
  }
  const head = text.slice(0, 2048).replace(/^﻿/, '').trimStart();
  try {
    if (head.startsWith('#EXTM3U')) {
      return prunePlaylist(text);
    }
    if (head.startsWith('<') && /<([\w-]+:)?MPD[\s>]/.test(head)) {
      return pruneDash(text);
    }
  } catch (e) {
  }
  return null;
}

function isManifest(url, type) {
  try {
    if (url && MANIFEST_PATH.test(new URLCtor(url, location.href).pathname)) {
      return true;
    }
  } catch (e) {
  }
  return !!type && MANIFEST_TYPE.test(type);
}

// --- fetch() ---

const nativeFetch = win.fetch;
if (typeof nativeFetch === 'function' && ResponseCtor) {
  const replaceResponse = (response) => {
    try {
      if (!response || !response.ok || response.type === 'opaque' ||
          !isManifest(response.url, response.headers.get('content-type'))) {
        return response;
      }
    } catch (e) {
      return response;
    }
    return response.clone().text().then(
        (text) => {
          const changed = rewrite(text);
          if (changed === null) {
            return response;
          }
          const headers = new HeadersCtor(response.headers);
          headers.delete('content-length');
          headers.delete('content-encoding');
          const replacement = new ResponseCtor(changed, {
            status: response.status,
            statusText: response.statusText,
            headers,
          });
          // Players resolve the segment addresses against the manifest's.
          for (const name of ['url', 'redirected', 'type']) {
            defineProperty(replacement, name, {value: response[name]});
          }
          return replacement;
        },
        () => response);
  };
  win.fetch = new NativeProxy(nativeFetch, {
    apply(target, thisArg, args) {
      return apply(target, thisArg, args).then(replaceResponse);
    },
  });
}

// --- XMLHttpRequest ---

// The prototype with the getters: XMLHttpRequest may already be a subclass
// (uBlock's XHR scriptlets install one).
let xhrProto = win.XMLHttpRequest && win.XMLHttpRequest.prototype;
while (xhrProto && !getOwnPropertyDescriptor(xhrProto, 'readyState')) {
  xhrProto = Object.getPrototypeOf(xhrProto);
}
const getterOf = (name) => {
  const descriptor = xhrProto && getOwnPropertyDescriptor(xhrProto, name);
  return descriptor && descriptor.get;
};
const readyStateOf = getterOf('readyState');
const statusOf = getterOf('status');
const responseTypeOf = getterOf('responseType');
const responseUrlOf = getterOf('responseURL');
const responseTextOf = getterOf('responseText');
const responseOf = getterOf('response');
if (xhrProto && readyStateOf && statusOf && responseTypeOf && responseUrlOf &&
    responseTextOf && responseOf) {
  const getResponseHeader = xhrProto.getResponseHeader;
  // Per request: the rewritten manifest, or null.
  const rewritten = new WeakMap();
  const buffers = new WeakMap();
  const documents = new WeakMap();

  const rewrittenText = (xhr) => {
    if (typeof xhr !== 'object' || xhr === null) {
      return null;
    }
    try {
      if (apply(readyStateOf, xhr, []) !== 4) {
        return null;
      }
      let text = apply(weakMapGet, rewritten, [xhr]);
      if (text !== undefined) {
        return text;
      }
      text = null;
      const status = apply(statusOf, xhr, []);
      const type = apply(responseTypeOf, xhr, []);
      if (status >= 200 && status < 300) {
        if (type === '' || type === 'text') {
          // Cheap: rewrite() looks at the start of the text first.
          text = rewrite(apply(responseTextOf, xhr, []));
        } else if (
            type === 'arraybuffer' &&
            isManifest(apply(responseUrlOf, xhr, []),
                       apply(getResponseHeader, xhr, ['content-type']))) {
          const buffer = apply(responseOf, xhr, []);
          if (buffer && buffer.byteLength <= MAX_MANIFEST_CHARS) {
            text = rewrite(new TextDecoderCtor().decode(buffer));
          }
        }
      }
      apply(weakMapSet, rewritten, [xhr, text]);
      return text;
    } catch (e) {
      return null;
    }
  };

  const wrapGetter = (name, replace) => {
    const descriptor = getOwnPropertyDescriptor(xhrProto, name);
    if (!descriptor || !descriptor.get) {
      return;
    }
    descriptor.get = new NativeProxy(descriptor.get, {
      apply(target, thisArg, args) {
        const text = rewrittenText(thisArg);
        return text === null ? apply(target, thisArg, args) :
                               replace(text, thisArg, target);
      },
    });
    defineProperty(xhrProto, name, descriptor);
  };
  wrapGetter('responseText', (text) => text);
  wrapGetter('response', (text, xhr, original) => {
    const type = apply(responseTypeOf, xhr, []);
    if (type === 'arraybuffer') {
      let buffer = apply(weakMapGet, buffers, [xhr]);
      if (!buffer) {
        buffer = new TextEncoderCtor().encode(text).buffer;
        apply(weakMapSet, buffers, [xhr, buffer]);
      }
      return buffer;
    }
    return type === '' || type === 'text' ? text : apply(original, xhr, []);
  });
  wrapGetter('responseXML', (text, xhr, original) => {
    if (apply(responseTypeOf, xhr, []) !== '') {
      return apply(original, xhr, []);
    }
    let doc = apply(weakMapGet, documents, [xhr]);
    if (!doc) {
      doc = new DOMParserCtor().parseFromString(text, 'application/xml');
      apply(weakMapSet, documents, [xhr, doc]);
    }
    return doc;
  });

  // A request object can be opened again for another request.
  xhrProto.open = new NativeProxy(xhrProto.open, {
    apply(target, thisArg, args) {
      if (typeof thisArg === 'object' && thisArg !== null) {
        apply(weakMapDelete, rewritten, [thisArg]);
        apply(weakMapDelete, buffers, [thisArg]);
        apply(weakMapDelete, documents, [thisArg]);
      }
      return apply(target, thisArg, args);
    },
  });
}
})();
