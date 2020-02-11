const assert = require("assert");
const path = require("path");
const vscode = require("vscode");
const os = require("os");
const cp = require("child_process");
const fs = require("fs-extra");
const { Uri } = vscode;

let root = path.dirname(path.dirname(__dirname));
let fixtureSrcDir = path.join(root, "fixtures");

suite("Basic tests", () => {
  test("Opam tests", async () => {
    if (process.platform == "win32" || process.platform == "win64") {
      return;
    }

    let sampleOpamSrc = path.join(fixtureSrcDir, "sample-opam");
    projectPath = path.join(os.tmpdir(), "sample-opam");
    let opamRoot = path.join(os.tmpdir(), "opam-root");
    fs.copySync(sampleOpamSrc, projectPath);
    cp.execSync(`mkdir -p ${opamRoot}`);
    cp.execSync(`sh -c 'opam install . --deps-only --yes > /dev/null'`, {
      cwd: projectPath
    }).toString();
    projectUri = Uri.file(projectPath);
    await vscode.commands.executeCommand("vscode.openFolder", projectUri);
    reasonDocument = await vscode.workspace.openTextDocument(
      Uri.file(path.join(projectPath, "foo.re"))
    );

    ocamlDocument = await vscode.workspace.openTextDocument(
      Uri.file(path.join(projectPath, "main.ml"))
    );

    // TODO: re-enable
    // assert.equal(
    //   reasonDocument.languageId,
    //   "reason",
    //   "Must be identified as a Reason document"
    // );

    assert.equal(
      ocamlDocument.languageId,
      "ocaml",
      "Must be identified as an OCaml document"
    );
    console.log("Cleaning up (opam)...");
    try {
      console.log("  Removing switch");
      console.log(cp.execSync("opam switch remove e2e --yes").toString());
      console.log("  Removing test project");
      fs.removeSync(projectPath);
    } catch (e) {}
  }).timeout(100000000000);
});
