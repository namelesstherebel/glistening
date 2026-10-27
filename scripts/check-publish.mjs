// prepublishOnly guard: the source checkout must never be published.
// validateBundle (package-contents.mjs) walks the whole root and rejects
// source files, so it is only valid on assembled staging, not here.
// bundle.mjs strips "scripts" from the staged package.json, so this guard
// does not run for the assembled release bundle.
// No network access, no mutation.
console.error(
  [
    "publish blocked: refusing to publish from the source root.",
    "The source checkout has no prebuilt binaries and is not a valid release.",
    "Assemble a release bundle instead:",
    "  node scripts/bundle.mjs artifacts dist",
    "Then verify the resulting .tgz and publish that approved tarball, not the source.",
  ].join("\n"),
);
process.exit(1);
