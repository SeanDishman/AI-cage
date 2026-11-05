# Credentials and repository contents

The repository contains source, build scripts, tests, design files and documentation. Sign-in credentials are supplied locally after setup; they are not part of the repository or desktop package.

Do not commit Codex `auth.json`, API keys, private keys, `.env` files, browser/Electron profiles, conversation transcripts, session recordings, logs, Docker volumes or generated build folders. The root `.gitignore` excludes these local files. Ignore rules do not remove files that were already tracked; check the staged file list before committing.

The `Secret scan` GitHub Actions workflow runs a pinned, checksum-verified Gitleaks binary against committed history on pushes and pull requests. To scan locally with Gitleaks installed:

```powershell
gitleaks git --no-banner --redact=100 --log-opts="--all" .
```

For sandbox boundaries and limitations, read [README.md](README.md). Do not include credentials or private chat logs in public issues or pull requests.
