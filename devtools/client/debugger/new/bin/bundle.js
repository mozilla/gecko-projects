const {
  tools: { makeBundle, copyFile }
} = require("devtools-launchpad/index");

const sourceMapAssets = require("devtools-source-map/assets");
const path = require("path");
const minimist = require("minimist");
var fs = require("fs");
const rimraf = require("rimraf");


const projectPath = path.resolve(__dirname, "..");
const bundlePath = path.join(projectPath, "./dist");

const clientPath = path.join(projectPath, "../../");
const watch = false;
const updateAssets = false;

process.env.NODE_ENV = "production"

function moveFile(src, dest, opts) {
  if (!fs.existsSync(src)) {
    return;
  }

  copyFile(src, dest, opts);
  rimraf.sync(src);
}

async function bundle() {

  makeBundle({
    outputPath: bundlePath,
    projectPath,
    watch,
    updateAssets,
    onFinish: () => onBundleFinish({bundlePath, projectPath })
  })
    .then(() => {
      console.log("[copy-assets] bundle is done")
    })
    .catch(err => {
      console.log(
        "[copy-assets] Uhoh, something went wrong. " +
          "The error was written to assets-error.log"
      );

      fs.writeFileSync("assets-error.log", JSON.stringify(err, null, 2));
    });
}

function onBundleFinish({ bundlePath, projectPath }) {
  console.log("[copy-assets] delete debugger.js bundle");

  const debuggerPath = path.join(bundlePath, "debugger.js");
  if (fs.existsSync(debuggerPath)) {
    fs.unlinkSync(debuggerPath);
  }

  console.log("[copy-assets] copy shared bundles to client/shared");
  moveFile(
    path.join(bundlePath, "source-map-worker.js"),
    path.join(clientPath, "shared/source-map/worker.js"),
    { cwd: projectPath }
  );
  for (const filename of Object.keys(sourceMapAssets)) {
    moveFile(
      path.join(bundlePath, "source-map-worker-assets", filename),
      path.join(clientPath, "shared/source-map/assets", filename),
      { cwd: projectPath }
    );
  }

  moveFile(
    path.join(bundlePath, "source-map-index.js"),
    path.join(clientPath, "shared/source-map/index.js"),
    { cwd: projectPath }
  );


  moveFile(
    path.join(bundlePath, "reps.js"),
    path.join(clientPath, "shared/components/reps/reps.js"),
    { cwd: projectPath }
  );

  moveFile(
    path.join(bundlePath, "reps.css"),
    path.join(clientPath, "shared/components/reps/reps.css"),
    { cwd: projectPath }
  );

  console.log("[copy-assets] done");
}

bundle();
