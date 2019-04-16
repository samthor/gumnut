

export async function initializeSideModule(path, callback) {
  const response = await window.fetch(path);
  const module = await WebAssembly.compileStreaming(response);

  const pages = 128;  // TODO: make configurable

  const memory = new WebAssembly.Memory({initial: pages, maximum: pages});
  const table = new WebAssembly.Table({initial: 2, maximum: 2, element: 'anyfunc'});
  const view = new Uint8Array(memory.buffer);

  const env = {
    memory,
    __memory_base: (pages - 1) * 65536,  // put Emscripten stack at end of memory
    table,
    __table_base: 0,
  };
  const importObject = {env};

  const methods = await callback(view);
  for (const method in methods) {
    importObject.env[method] = methods[method];
  }

  const instance = await WebAssembly.instantiate(module, importObject);

  // emscripten _post_instantiate
  instance.exports.__post_instantiate && instance.exports.__post_instantiate();

  return {instance, view};
}


/**
 * Incredibly simple malloc implementation.
 */
export class Alloc {
  constructor(view) {
    this._view = view;

    // [2n+0,2n+1]: free ranges
    // [2n+1,2n+2]: used ranges
    // e.g.:
    //   0,100: all free
    //   0,0,10,100: 0-0 free, 0-10 use, 10-100 free
    this._record = [0, view.length];
  }

  malloc(req) {
    const r = this._record;
    for (let i = 0; i < r.length; i += 2) {
      const cap = r[i+1] - r[i];
      if (req <= cap) {
        r.splice(i+1, 0, r[i], r[i] + req);
        return r[i];
      }
    }
    throw new Error(`can't alloc ${req} bytes`);
  }

  free(at) {
    const r = this._record;
    for (let i = 1; i < r.length - 1; i += 2) {
      if (r[i] === at) {
        r.splice(i, 2);
        return;
      }
    }
    throw new Error(`not allocated at ${at}`);
  }
}
