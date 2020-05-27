

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
