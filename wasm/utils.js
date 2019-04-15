
const encoder = new TextEncoder();
const decoder = new TextDecoder();

export async function createWebAssembly(path, importObject) {
  const bytes = await window.fetch(path).then(x => x.arrayBuffer());
  const object = await WebAssembly.instantiate(bytes, importObject);
  object.importObject = importObject;
  return object;
}

export function encodeToArray(s, array) {
  let i = 0;
  for (let si = 0; si < s.length; ++si) {
    const c = s.charCodeAt(si);
    if (c >= 128) {
      throw new Error('non-ascii not yet supported: ' + c);
    }
    array[i++] = c;
  }
  array[i++] = 0;
}

export class CodeView {
  constructor(memory, s) {
    const array = new Uint8Array(memory.buffer);

    const bytes = encoder.encode(s);
    const startAt = array.length - (bytes.length + 1);

    const view = array.subarray(startAt);
    view.set(bytes);
    array[array.length - 1] = 0;  // EOF

    this._view = view;
    this.at = startAt;
  }

  read(at, len) {
    const backing = this._view.slice(at, at + len);
    return decoder.decode(backing);
  }
}
