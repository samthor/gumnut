foo: for (;;) {

  for (;;) {
    break  // "foo" needs to be on same line
    foo;
  }

  console.info('escape');
  break;
}

console.info('whatever');
