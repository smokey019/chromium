// Copyright 2026 The REBA Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Signs resources.json for the browser, which only uses a copy it can verify.
//
//   node sign_resources.mjs resources.json <private key (PKCS#8 PEM, P-256)>
//
// Writes resources.json.sig: the base64 of a DER encoded ECDSA signature of
// the file with SHA-256. Publish both next to each other.

import {createPrivateKey, sign} from 'node:crypto';
import {readFileSync, writeFileSync} from 'node:fs';

const [file, keyFile] = process.argv.slice(2);
if (!file || !keyFile) {
  console.error('usage: sign_resources.mjs <resources.json> <private key>');
  process.exit(2);
}
const key = createPrivateKey(readFileSync(keyFile));
const signature = sign('sha256', readFileSync(file), {key, dsaEncoding: 'der'});
writeFileSync(`${file}.sig`, signature.toString('base64'));
console.log(`${file}.sig written`);
