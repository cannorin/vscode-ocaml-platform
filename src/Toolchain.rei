type t;
let setup:
  (~env: Js.Dict.t(string), string) => Js.Promise.t(result(t, string));
let lsp: t => (string, array(string));
