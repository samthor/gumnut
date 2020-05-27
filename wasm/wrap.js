

async function initialize(modulePromise, callback, pages = 128) {
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

  const module = await modulePromise;
  let instance = await WebAssembly.instantiate(module, importObject);

  // emscripten _post_instantiate
  if (!instance.exports && instance.instance) {
    instance = instance.instance;
  }
  instance.exports.__post_instantiate && instance.exports.__post_instantiate();

  return {instance, view};
}

export default async function build(modulePromise) {
  const writeAt = 1024;
  let handler = null;

  const {instance, view} = await initialize(modulePromise, (view) => {
    return {
      _memset(s, c, n) {
        view.fill(c, s, s + n);
        return s;
      },
  
      _memcpy(dst, src, size) {
        view.set(view.subarray(src, src + size), dst);
        return dst;
      },
  
      abort(x) {
        throw x;
      },
  
      _token_callback(p, len, line_no, type, special) {
        handler(p, len, line_no, type, special);
      },
    };
  });

  const {exports} = instance;

  /**
   * @param {!Buffer|!Uint8Array} buffer
   * @param {!Function} callback
   */
  const run = (buffer, callback) => {
    view.set(buffer, writeAt);
    view[buffer.length + writeAt] = 0;  // NULL terminate

    exports._xx_setup(writeAt);
    handler = (p, len, lineNo, type, special) => {
      callback(p - writeAt, len, lineNo, type, special);
      // if (special) {
      //   callback(p, len);
      // }
    };

    let ret = 0;
    for (;;) {
      ret = exports._xx_run();
      if (ret <= 0) {
        break;
      }
    }
    if (ret < 0) {
      throw new TypeError(`internal error: ${ret}`);
    }
  };

  return run;
}

