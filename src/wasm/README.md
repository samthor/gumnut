Rewrites ESM import paths.
Powered by Web Assembly via [prsr](https://github.com/samthor/prsr).

# Usage

```js
import build from 'module-rewriter';

const resolve = (importee, importer) => {
  // importee is from import/export, importer is the current file
  // TODO: this is a terrible resolver, don't do this
  return `/node_modules/${importee}/index.js`;
};

build(resolve).then((rewriter) => {
  rewriter('input.js', process.stdout);
});

```

Currently just reads a file from disk and writes it to a stream.
