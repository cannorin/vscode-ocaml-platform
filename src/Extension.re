open LSP;
open Bindings;

let then_ = f =>
  Js.Promise.then_(
    fun
    | Ok(x) => f(x)
    | Error(e) => Js.Promise.resolve(Error(e)),
  );

let handleError:
  (string => Js.Promise.t(unit), Js.Promise.t(result(unit, string))) =>
  Js.Promise.t(unit) =
  f =>
    Js.Promise.then_(
      fun
      | Ok () => Js.Promise.resolve()
      | Error(msg) => f(msg),
    );

let activate = _context => {
  Js.Dict.set(Process.env, "OCAMLRUNPARAM", "b");
  Js.Dict.set(Process.env, "MERLIN_LOG", "-");
  let folder = Workspace.rootPath;
  Toolchain.setup(~env=Process.env, folder)
  |> then_(toolchain => {
       let serverOptions = Server.make(toolchain);
       let client =
         LanguageClient.make(
           ~id="ocaml",
           ~name="OCaml Language Server",
           ~serverOptions,
           ~clientOptions=Client.make(),
         );
       client.start(.);
       Js.Promise.resolve(Ok());
     })
  |> handleError(Window.showErrorMessage)
  |> Js.Promise.catch(e => {
       let message = Bindings.Error.ofPromiseError(e);
       Window.showErrorMessage({j|Error: $message|j});
     });
};
