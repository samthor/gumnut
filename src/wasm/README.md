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
  const s = rewriter('input.js');
  s.pipe(process.stdout);
});

```

Try out `demo.js` in the repo or NPM install.

