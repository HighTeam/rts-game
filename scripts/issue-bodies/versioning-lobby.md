## Versioning & Lobby Compatibility

Epic from [docs/BACKLOG.md](../../docs/BACKLOG.md). Owner controls when the version number increases.

### Version identity

- [ ] Single source of truth for game version (build + runtime)
- [ ] Show version in main menu / lobby
- [ ] **Do not bump version unless the owner says so**

### Lobby enforcement

- [ ] Host advertises required / protocol version
- [ ] Mismatched joiners rejected with a clear message
- [ ] Default policy: exact version match (e.g. all `1.4.5` together; `1.4.6` cannot join)
- [ ] Document wire fields in DECISIONS.md / net docs
