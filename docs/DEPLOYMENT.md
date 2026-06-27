# FocoFlow Deployment

> **Note:** As of v0.2, FocoFlow ships as a **Tauri desktop app**. The Docker / Spring Boot deployment path has been removed.

## Desktop build

```bash
npm install
cd frontend && npm install && cd ..
npm run tauri:build
```

Artifacts land in `src-tauri/target/release/bundle/`.

## macOS permissions

Users must grant **Accessibility** and **Input Monitoring** before capture works. Document this in release notes.

## CI

See `.github/workflows/ci.yml` — Python training tests, frontend checks, and `cargo check` / `cargo test`.
