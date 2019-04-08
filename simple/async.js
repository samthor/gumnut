var async = 123;
var await = (v) => console.info(v);
console.info(await);

x = async () => await
/123/;

const out = x();
console.info(out);
out.then((x) => {
  console.info('result from Promise', x);
});
