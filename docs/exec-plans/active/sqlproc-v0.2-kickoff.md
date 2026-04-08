# SQL Processor v0.2 Kickoff

이 문서는 현재 동작 계약이 아니라, v0.2 kickoff 당시의 구현 진행 순서를 요약한 기록입니다.

- Sprint 0: repo contracts, build scripts, fixtures, evaluator loop를 scaffold했습니다. Done 기준은 `make build`, `make test`가 연결되는 것입니다. Verify with `make build`, `make test`.
- Sprint 1: `INSERT`와 `SELECT` projection을 위한 tokenizer, parser, AST를 구현했습니다. Done 기준은 parser tests passing입니다. Verify with `make test`.
- Sprint 2: schema loading, CSV storage, executor, CLI flow를 구현했습니다. Done 기준은 demo query가 end to end로 실행되는 것입니다. Verify with `make demo`.
- Sprint 3: golden outputs, README demo flow, fixture isolation을 고정했습니다. Done 기준은 `make check` passing입니다. Verify with `make check`.
- Stretch: `--emit-ast` debug output은 core gate가 green이 된 뒤 추가했습니다. 현재는 구현되어 있으며 stdout contract를 유지하기 위해 stderr로 출력합니다.

## v0.2 보강: 영구 반영 데모 DB

- `sample_db/`는 수동 데모용 checked-in example DB이며 `make demo` 또는 `./sqlproc --db ./sample_db ...` 실행으로 변경될 수 있습니다.
- `tests/fixtures/sample_db_seed/`는 `make check`와 deterministic verification을 위한 immutable seed로 유지합니다.
- 이번 정리는 SQL subset 확장이 아니라 기존 `INSERT`의 file persistence contract를 더 명시적으로 드러내는 작업입니다.
