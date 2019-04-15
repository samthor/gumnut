
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

export function copyToMemory(memory, s) {
  const array = new Uint8Array(memory.buffer);
  const startAt = array.length - ((s.length + 1) * 4);
  const writeTo = new Uint8Array(memory.buffer, startAt);
  encodeToArray(s, writeTo);
  return startAt;
}
