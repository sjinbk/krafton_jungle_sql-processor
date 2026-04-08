# Code Walkthrough For Presentation

이 문서는 저장소를 처음 보는 사람을 위한 발표 보조 자료다. 각 모듈이 어떤 질문에 답하는지 설명하고, 코드 흐름에 맞는 발표 순서를 제안한다.

## 한 줄 요약

이 프로젝트는 file-backed database를 대상으로 하는 작은 SQL processor MVP다. 핵심 강점은 기능 수보다도, 동작 계약과 테스트, golden output, fixture 규칙이 같은 범위를 함께 지지한다는 점이다.

## 발표 시작 문장 예시

"이 프로젝트는 지원하는 SQL 범위는 좁지만, 동작 계약과 테스트, demo 흐름, golden output, fixture isolation이 서로 맞물리도록 의도적으로 설계된 harness-first 저장소입니다."

## 코드 읽기 순서

1. `src/main.c`
   - 질문: SQL 파일 하나가 프로그램 안에서 어떻게 흘러가는가?
   - 먼저 read file, split statements, parse, execute, report errors의 큰 흐름을 보여 주면 좋다.
2. `src/input.c`
   - 질문: statement 하나는 어디서 끝나고 다음 statement는 어디서 시작하는가?
   - tokenization 전에 comments, string literals, semicolons를 왜 먼저 처리해야 하는지 설명한다.
3. `src/tokenizer.c`
   - 질문: parser가 필요로 하는 최소 token stream은 무엇인가?
   - 문법 범위를 의도적으로 좁게 잡았다는 점을 보여 주기 좋다.
4. `src/parser.c`
   - 질문: SQL 텍스트는 어떻게 AST가 되는가?
   - default schema 처리, projection parsing, multi-row INSERT parsing을 강조한다.
5. `src/schema.c`
   - 질문: table 구조는 어디서 오는가?
   - `.schema` 파일이 구조적 source of truth라는 점을 설명한다.
6. `src/storage.c`
   - 질문: row는 어떻게 저장되고 다시 읽히는가?
   - CSV quoting 규칙과 storage가 SQL 의미 자체를 이해하려 하지 않는 이유를 설명한다.
7. `src/executor.c`
   - 질문: SQL 의미 검사는 어디서 강제되는가?
   - 타입 검사, column 매칭, result formatting이 여기서 일어난다.
8. `src/ast.c`와 `src/error.c`
   - 질문: 공통 상태는 어떻게 정리되고 실패는 어떻게 보고되는가?
9. `tests/test_main.c`
   - 질문: 어떤 계약이 테스트로 보호되는가?
10. `queries/*.sql`
   - 질문: 발표자가 보여 줄 demo와 failure 시나리오는 무엇인가?

## 모듈별 핵심 메시지

### main

- `main`은 business logic 모듈이 아니라 오케스트레이터다.
- 첫 실패 statement에서 멈추기 때문에 실행 순서와 오류 처리 규칙을 설명하기 쉽다.

### input

- splitter는 parser와 다른 문제를 해결한다.
- `;`가 statement의 끝인지, 아니면 string literal 일부인지 먼저 구분해야 한다.
- 이후 parser 오류가 올바른 위치를 가리키도록 source 좌표도 함께 보존한다.

### tokenizer와 parser

- tokenizer는 MVP 문법에 실제로 필요한 token만 만든다.
- parser는 INSERT와 SELECT만 허용해 지원 범위를 명시적으로 유지한다.

### schema와 storage

- `.schema`는 table shape를 정의한다.
- `.csv`는 실제 row 데이터를 저장한다.
- storage 코드는 파일 포맷 규칙만 다루고, 의미 검사는 executor에 남겨 둔다.

### executor

- INSERT 실행은 "이 값들이 이 schema와 맞는가?"에 답한다.
- SELECT 실행은 "어떤 column을 보여 주고 결과를 어떻게 포맷할 것인가?"에 답한다.
- 이 분리 덕분에 syntax error와 semantic error를 발표 중에도 쉽게 구분할 수 있다.

## 코드리뷰에서 강조할 장점

- 범위가 작고 그 범위가 일관되게 강제된다.
- parser 책임과 executor 책임이 명확히 분리돼 있다.
- golden output과 fixture isolation 덕분에 demo 재현성이 좋다.
- `--emit-ast`는 stdout 계약을 깨지 않으면서 디버그 가시성을 더해 준다.

## 솔직하게 말하면 좋은 한계

- WHERE, UPDATE, DELETE, JOIN, ORDER BY, GROUP BY, LIMIT은 지원하지 않는다.
- 값 타입은 INT와 TEXT만 지원한다.
- quoted identifier, expression, NULL, 더 넓은 SQL 타입 지원은 없다.
- SELECT는 전체 table을 메모리로 읽기 때문에 대규모 엔진이 아니라 MVP 설계에 가깝다.

## 추천 발표 순서

1. `docs/spec/mvp.md`로 지원 범위를 먼저 정의한다.
2. 이 문서와 `README.md`로 코드 읽기 순서를 안내한다.
3. `src/main.c`에서 전체 실행 경로를 고정한다.
4. `src/input.c`와 `src/parser.c`로 SQL이 구조로 바뀌는 과정을 설명한다.
5. `src/schema.c`, `src/storage.c`, `src/executor.c`로 구조가 실제 동작이 되는 과정을 설명한다.
6. `queries/demo.sql`과 `make demo` 결과로 happy path를 보여 준다.
7. `queries/error.sql`, `queries/parse_error.sql`, `queries/type_error.sql`로 failure contract를 보여 준다.
8. `tests/test_main.c`와 `docs/testing/test-matrix.md`로 계약이 실제로 검증된다는 점으로 마무리한다.

## 발표 마무리 문장 예시

"기능 범위는 의도적으로 작지만, 입력 계약과 출력 계약, 오류 계약, 테스트 계약이 같은 MVP 동작을 중심으로 맞물리기 때문에 이 저장소는 설명 가능성과 재현성이 높습니다."
