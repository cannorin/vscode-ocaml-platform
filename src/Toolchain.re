module Caml = {
  module Array = Array;
};
module R = Result;
open Tablecloth;
open Bindings;
open Utils;

let pathMissingFromEnv = "'PATH' variable not found in the environment";
let noPackageManagerFound = {j| No package manager found. We support opam (https://opam.ocaml.org/) and esy (https://esy.sh/) |j};

type commandAndArgs = (string, array(string));

module type MTYPE = {
  type t;
  let make: unit => t;
  let onProgress: (t, float => unit) => unit;
  let onEnd: (t, unit => unit) => unit;
  let onError: (t, string => unit) => unit;
  let reportProgress: (t, float) => unit;
  let reportEnd: t => unit;
  let reportError: (t, string) => unit;
  let run: (t, string) => Js.Promise.t(unit);
};

let setupWithProgressIndicator = (m, folder) => {
  module M = (val m: MTYPE);
  M.(
    Window.withProgress(
      {
        "location": 15, /* Window.(locationToJs(Notification)) */
        "title": "Setting up toolchain...",
      },
      progress => {
        let succeeded = ref(Ok());
        let eventEmitter = make();
        onProgress(eventEmitter, percent => {
          progress.report(. {"increment": int_of_float(percent *. 100.)})
        });
        onEnd(eventEmitter, () => {progress.report(. {"increment": 100})});
        onError(eventEmitter, errorMsg => {succeeded := Error(errorMsg)});
        Js.Promise.(
          run(eventEmitter, folder) |> then_(() => resolve(succeeded^))
        );
      },
    )
  );
};

module Cmd: {
  type t;
  let make:
    (~env: Js.Dict.t(string), ~cmd: string) =>
    Js.Promise.t(result(t, string));
  type stdout = string;
  let output:
    (~args: Js.Array.t(string), ~cwd: string, t) =>
    Js.Promise.t(result(stdout, string));
} = {
  type t = {
    cmd: string,
    env: Js.Dict.t(string),
  };
  type stdout = string;
  let make = (~env, ~cmd) => {
    let cmd = Sys.unix ? cmd : cmd ++ ".cmd";
    switch (Js.Dict.get(env, "PATH")) {
    | None => Error(pathMissingFromEnv) |> Js.Promise.resolve
    | Some(path) =>
      String.split(~on=env_sep, path)
      |> Array.fromList
      |> Js.Array.map(p => Fs.exists(Filename.concat(p, cmd)))
      |> Js.Promise.all
      |> Js.Promise.then_(Js.Promise.resolve << Js.Array.filter(x => x))
      |> Js.Promise.then_(
           Js.Promise.resolve
           << (
             l =>
               Js.Array.length(l) == 0
                 ? Error({j| Command "$cmd" not found |j}) : Ok({cmd, env})
           ),
         )
    };
  };
  let output = (~args, ~cwd, {cmd, env}) => {
    let shellString =
      Js.Array.concat(args, [|cmd|]) |> Js.Array.joinWith(" ");
    Js.log(shellString);
    ChildProcess.exec(shellString, ChildProcess.Options.make(~cwd, ~env, ()))
    |> Js.Promise.then_(
         Js.Promise.resolve
         << (
           fun
           | Error(e) => e |> ChildProcess.E.toString |> R.fail
           | Ok((exitCode, stdout, stderr)) =>
             if (exitCode == 0) {
               Ok(stdout);
             } else {
               Error(
                 {j| Command $cmd failed:
exitCode: $exitCode
stderr: $stderr
|j},
               );
             }
         ),
       );
  };
};

/* Package managers that can install native Reason/
   OCaml packages for us */
module PackageManager: {
  type t;
  type spec;
  module type T = {
    let name: string;
    let lockFile: Fpath.t;
    let make:
      (
        ~env: Js.Dict.t(string),
        ~root: Fpath.t,
        ~discoveredManifestPath: Fpath.t
      ) =>
      Js.Promise.t(result(spec, string));
  };
  module Esy: T;
  module Opam: T;
  let ofName:
    (
      ~env: Js.Dict.t(string),
      ~name: string,
      ~root: Fpath.t,
      ~discoveredManifestPath: Fpath.t
    ) =>
    Js.Promise.t(result(spec, string));
  let make:
    (~env: Js.Dict.t(string), ~discoveredManifestPath: Fpath.t, ~t: t) =>
    Js.Promise.t(result(spec, string));
  let setupToolChain: spec => Js.Promise.t(result(unit, string));
  let alreadyUsed: Fpath.t => Js.Promise.t(result(list(t), string));
  let available:
    (~env: Js.Dict.t(string), list(t)) =>
    Js.Promise.t(result(list(t), string));
  let env: spec => Js.Promise.t(result(Js.Dict.t(string), string));
  let lsp: spec => commandAndArgs;
  module Manifest: {
    let lookup: Fpath.t => Js.Promise.t(result(list(t), string));
  };
} = {
  type t =
    | Opam(Fpath.t)
    | Esy(Fpath.t);

  type spec = {
    cmd: Cmd.t,
    lsp: unit => commandAndArgs,
    env: unit => Js.Promise.t(result(Js.Dict.t(string), string)),
    setup: unit => Js.Promise.t(result(unit, string)),
  };

  module type T = {
    let name: string;
    let lockFile: Fpath.t;
    let make:
      (
        ~env: Js.Dict.t(string),
        ~root: Fpath.t,
        ~discoveredManifestPath: Fpath.t
      ) =>
      Js.Promise.t(result(spec, string));
  };

  module Esy: T = {
    let name = "esy";
    let lockFile = Fpath.(v("esy.lock") / "index.json");
    let make = (~env, ~root, ~discoveredManifestPath) =>
      Cmd.make(~cmd="esy", ~env)
      |> okThen(cmd => {
           {
             cmd,
             setup: () => {
               let rootStr = root |> Fpath.toString;
               Cmd.output(
                 cmd,
                 ~args=[|"status", "-P", rootStr|],
                 ~cwd=rootStr,
               )
               |> okThen(stdout => {
                    switch (Json.parse(stdout)) {
                    | None => R.return(false)
                    | Some(json) =>
                      json
                      |> Json.Decode.(field("isProjectReadyForDev", bool))
                      |> R.return
                    }
                  })
               |> Js.Promise.then_(
                    fun
                    | Error(e) => e |> R.fail |> Js.Promise.resolve
                    | Ok(isProjectReadyForDev) =>
                      if (isProjectReadyForDev) {
                        () |> R.return |> Js.Promise.resolve;
                      } else if (root == discoveredManifestPath) {
                        setupWithProgressIndicator(
                          (module Setup.Esy),
                          rootStr,
                        );
                      } else {
                        setupWithProgressIndicator(
                          (module Setup.Bsb),
                          rootStr,
                        );
                      },
                  );
             },
             env: () =>
               Cmd.output(
                 cmd,
                 ~args=[|
                   "command-env",
                   "--json",
                   "-P",
                   Fpath.toString(root),
                 |],
                 ~cwd=Fpath.toString(root),
               )
               |> okThen(stdout => {
                    switch (Json.parse(stdout)) {
                    | Some(json) =>
                      json |> Json.Decode.dict(Json.Decode.string) |> R.return
                    | None =>
                      Error(
                        "'esy command-env' returned non-json output: "
                        ++ stdout,
                      )
                    }
                  }),
             lsp: () => (
               name,
               [|
                 "-P",
                 Fpath.toString(root),
                 "exec-command",
                 "--include-current-env",
                 "ocamllsp",
               |],
             ),
           }
           |> R.return
         });
  };

  module Opam: T = {
    let name = "opam";
    let lockFile = Fpath.v("opam.lock");
    let make = (~env, ~root, ~discoveredManifestPath) => {
      let rootStr = root |> Fpath.toString;
      Cmd.make(~cmd="opam", ~env)
      |> okThen(cmd => {
           {
             cmd,
             setup: () => {
               /* TODO: check if tools needed are available in the opam switch */
               setupWithProgressIndicator(
                 (module Setup.Opam),
                 rootStr,
               );
             },

             env: () =>
               Cmd.output(
                 cmd,
                 ~args=[|"exec", "env"|], /* TODO: Windows? */
                 ~cwd=Fpath.toString(root),
               )
               |> okThen(stdout => {
                    stdout
                    |> String.split(~on="\n")
                    |> List.map(~f=x => String.split(~on="=", x))
                    |> List.map(
                         ~f=
                           fun
                           | [] =>
                             Error("Splitting on '=' in env output failed")
                           | [k, v] => Ok((k, v))
                           | l => {
                               Js.log(l);
                               Error(
                                 "Splitting on '=' in env output returned more than two items",
                               );
                             },
                       )
                    |> List.foldLeft(
                         ~f=
                           (kv, acc) => {
                             switch (kv) {
                             | Ok(kv) => [kv, ...acc]
                             | Error(msg) =>
                               Js.log(msg);
                               acc;
                             }
                           },
                         ~initial=[],
                       )
                    |> Js.Dict.fromList
                    |> R.return
                  }),
             lsp: () => (name, [|"exec", "ocamllsp"|]),
           }
           |> R.return
         });
    };
  };

  let make = (~env, ~discoveredManifestPath, ~t) =>
    switch (t) {
    | Opam(root) => Opam.make(~env, ~root, ~discoveredManifestPath)
    | Esy(root) => Esy.make(~env, ~root, ~discoveredManifestPath)
    };

  let ofName = (~env, ~name, ~root, ~discoveredManifestPath) =>
    switch (name) {
    | x when x == Opam.name => Opam.make(~env, ~root, ~discoveredManifestPath)
    | x when x == Esy.name => Esy.make(~env, ~root, ~discoveredManifestPath)
    | _ => "Invalid package manager name" |> R.fail |> Js.Promise.resolve
    };

  let alreadyUsed = folder => {
    [Esy(folder), Opam(folder)]
    |> Array.fromList
    |> Array.map(~f=pm => {
         let lockFileFpath =
           switch (pm) {
           | Opam(root) => Fpath.(join(root, Opam.lockFile))
           | Esy(root) => Fpath.(join(root, Esy.lockFile))
           };
         Fs.exists(Fpath.show(lockFileFpath))
         |> Js.Promise.then_(Js.Promise.resolve << (exists => (pm, exists)));
       })
    |> Js.Promise.all
    |> Js.Promise.then_(l =>
         l
         |> Array.filter(~f=((_pm, used)) => used)
         |> Array.map(~f=t => {
              let (pm, _used) = t;
              pm;
            })
         |> Array.toList
         |> R.return
         |> Js.Promise.resolve
       );
  };
  let available = (~env, supportedPackageManagers) => {
    supportedPackageManagers
    |> List.map(~f=(pm: t) => {
         let name =
           switch (pm) {
           | Opam(_) => Opam.name
           | Esy(_) => Esy.name
           };
         Cmd.make(~env, ~cmd=name)
         |> Js.Promise.then_(
              Js.Promise.resolve
              << (
                fun
                | Ok(_) => {
                    (pm, true);
                  }
                | Error(e) => {
                    (pm, false);
                  }
              ),
            );
       })
    |> Array.fromList
    |> Js.Promise.all
    |> Js.Promise.then_(
         Js.Promise.resolve
         << R.return
         << Array.toList
         << Array.map(~f=t => {
              let (pm, _used) = t;
              pm;
            })
         << Array.filter(~f=((_pm, available)) => available),
       );
  };

  let setupToolChain = spec => spec.setup();
  let lsp = spec => spec.lsp();
  let env = spec => spec.env();

  module Manifest: {
    let lookup: Fpath.t => Js.Promise.t(result(list(t), string));
  } = {
    /* TODO: Constructors Esy and Opam simply take Fpath.t - no way of telling if that is a directory or actual filename */
    let parse = projectRoot =>
      fun
      | "esy.json" => Some(Esy(Fpath.(projectRoot))) |> Js.Promise.resolve
      | "opam" => {
          Fs.stat(Fpath.(projectRoot / "opam" |> toString))
          |> Js.Promise.then_(
               Js.Promise.resolve
               << (
                 fun
                 | Ok(stats) =>
                   Fs.Stat.isDirectory(stats)
                     ? None : Some(Opam(Fpath.(projectRoot)))
                 | Error(_) => None
               ),
             );
        }
      | "package.json" => {
          let manifestFile =
            Fpath.(projectRoot / "package.json") |> Fpath.show;
          Fs.readFile(manifestFile)
          |> Js.Promise.then_(manifest => {
               switch (Json.parse(manifest)) {
               | None => None |> Js.Promise.resolve
               | Some(json) =>
                 if (Utils.propertyExists(json, "dependencies")
                     || Utils.propertyExists(json, "devDependencies")) {
                   if (Utils.propertyExists(json, "esy")) {
                     Some(Esy(Fpath.(projectRoot / "package.json")))
                     |> Js.Promise.resolve;
                   } else {
                     Some(Esy(Fpath.(projectRoot / ".vscode" / "esy")))
                     |> Js.Promise.resolve;
                   };
                 } else {
                   None |> Js.Promise.resolve;
                 }
               }
             });
        }
      | file => {
          let manifestFile = Fpath.(projectRoot / file) |> Fpath.show;
          switch (Path.extname(file)) {
          | ".json" =>
            Fs.readFile(manifestFile)
            |> Js.Promise.then_(manifest => {
                 switch (Json.parse(manifest)) {
                 | Some(json) =>
                   if (Utils.propertyExists(json, "dependencies")
                       || Utils.propertyExists(json, "devDependencies")) {
                     Some(Esy(projectRoot)) |> Js.Promise.resolve;
                   } else {
                     None |> Js.Promise.resolve;
                   }

                 | None =>
                   Js.log3(
                     "Invalid JSON file found. Ignoring...",
                     manifest,
                     manifestFile,
                   );
                   None |> Js.Promise.resolve;
                 }
               })
          | ".opam" =>
            Fs.readFile(manifestFile)
            |> Js.Promise.then_(
                 fun
                 | "" => None |> Js.Promise.resolve
                 | _ => Some(Opam(projectRoot)) |> Js.Promise.resolve,
               )

          | _ => None |> Js.Promise.resolve
          };
        };

    let lookup = projectRoot => {
      Fs.readDir(Fpath.toString(projectRoot))
      |> Js.Promise.then_(
           fun
           | Error(msg) => Js.Promise.resolve(Error(msg))
           | Ok(files) => {
               files
               |> Js.Array.map(parse(projectRoot))
               |> Js.Promise.all
               |> Js.Promise.then_(l =>
                    Ok(
                      Js.Array.reduce(
                        (acc, x) =>
                          Js.Array.concat(
                            acc,
                            switch (x) {
                            | Some(x) => Array.fromList([x])
                            | None => Array.empty
                            },
                          ),
                        Array.empty,
                        l,
                      )
                      |> Array.toList,
                    )
                    |> Js.Promise.resolve
                  );
             },
         );
    };
  };
};

/* We declare two types with identical structure to differentiate state: t is the toolchain ready for consumption. resources is the toolchain that could need setup (and may fail) */

type t = {
  spec: PackageManager.spec,
  projectRoot: Fpath.t,
};

type resources = {
  spec: PackageManager.spec,
  projectRoot: Fpath.t,
};

let init = (~env, ~folder) => {
  let projectRoot = Fpath.ofString(folder);
  PackageManager.alreadyUsed(projectRoot)
  |> Js.Promise.then_(
       fun
       | Ok([]) =>
         PackageManager.Manifest.lookup(projectRoot)
         |> okThen(
              fun
              | [] => Error("TODO: global toolchain")
              | x => Ok(x),
            )
       | Ok(packageManagersInUse) =>
         Ok(packageManagersInUse) |> Js.Promise.resolve
       | Error(msg) => Error(msg) |> Js.Promise.resolve,
     )
  |> Js.Promise.then_(
       fun
       | Error(msg) => Js.Promise.resolve(Error(msg))
       | Ok(packageManagersInUse) => {
           PackageManager.available(~env, packageManagersInUse);
         },
     )
  |> Js.Promise.then_(
       fun
       | Error(e) => Js.Promise.resolve(Error(e))
       | Ok(packageManagersInUse) =>
         switch (packageManagersInUse) {
         | [] => Error(noPackageManagerFound) |> Js.Promise.resolve
         | [obviousChoice] =>
           PackageManager.make(
             ~env,
             ~t=obviousChoice,
             ~discoveredManifestPath=projectRoot,
           )
         | _multipleChoices =>
           let config = Vscode.Workspace.getConfiguration("ocaml");
           switch (
             Js.Nullable.toOption(config##packageManager),
             Js.Nullable.toOption(config##toolChainRoot),
           ) {
           /* TODO: unsafe type config. It's just 'a */
           | (Some(name), Some(root)) =>
             PackageManager.ofName(
               ~env,
               ~name,
               ~root,
               ~discoveredManifestPath=projectRoot,
             )
           | _ =>
             Error("TODO: Implement prompting choice of package manager")
             |> Js.Promise.resolve
           };
         },
     )
  |> okThen(spec => Ok({spec, projectRoot}));
};

let setup = ({spec, projectRoot}) => {
  PackageManager.setupToolChain(spec)
  |> Js.Promise.then_(
       fun
       | Error(msg) => Error(msg) |> Js.Promise.resolve
       | Ok () => PackageManager.env(spec),
     )
  |> Js.Promise.then_(
       fun
       | Ok(env) => Cmd.make(~cmd="ocamllsp", ~env)
       | Error(e) => e |> R.fail |> Js.Promise.resolve,
     )
  |> Js.Promise.then_(
       Js.Promise.resolve
       << (
         fun
         | Ok(_) => Ok({spec, projectRoot}: t)
         | Error(msg) => Error({j| Toolchain initialisation failed: $msg |j})
       ),
     );
};

let lsp = (t: t) => PackageManager.lsp(t.spec);
