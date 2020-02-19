// Generated by BUCKLESCRIPT, PLEASE EDIT WITH CARE
'use strict';

var Block = require("bs-platform/lib/js/block.js");
var Curry = require("bs-platform/lib/js/curry.js");
var Vscode = require("vscode");
var Toolchain = require("./Toolchain.bs.js");

function setupWithProgressIndicator(m, folder) {
  return Vscode.window.withProgress({
              location: 15,
              title: "Setting up toolchain..."
            }, (function (progress) {
                var succeeded = {
                  contents: /* Ok */Block.__(0, [/* () */0])
                };
                var eventEmitter = Curry._1(m.make, /* () */0);
                Curry._2(m.onProgress, eventEmitter, (function (percent) {
                        return progress.report({
                                    increment: percent * 100 | 0
                                  });
                      }));
                Curry._2(m.onEnd, eventEmitter, (function (param) {
                        return progress.report({
                                    increment: 100
                                  });
                      }));
                Curry._2(m.onError, eventEmitter, (function (errorMsg) {
                        succeeded.contents = /* Error */Block.__(1, [errorMsg]);
                        return /* () */0;
                      }));
                return Curry._2(m.run, eventEmitter, folder).then((function (param) {
                              return Promise.resolve(succeeded.contents);
                            }));
              }));
}

function make(toolchain) {
  var match = Toolchain.lsp(toolchain);
  return {
          command: match[0],
          args: match[1],
          options: {
            env: process.env
          }
        };
}

var Server = {
  setupWithProgressIndicator: setupWithProgressIndicator,
  make: make
};

function make$1(param) {
  return {
          documentSelector: /* array */[
            {
              scheme: "file",
              language: "ocaml"
            },
            {
              scheme: "file",
              language: "reason"
            }
          ]
        };
}

var Client = {
  make: make$1
};

exports.Server = Server;
exports.Client = Client;
/* vscode Not a pure module */