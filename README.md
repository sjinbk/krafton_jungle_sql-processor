# SQL-Processor
사용자가 SQL 파일을 넣으면, 문장별로 나누고, 토큰으로 쪼개고, AST라는 구조로 해석하여, csv파일에 INSERT와 SELECT를 수행한다.

```mermaid
flowchart TD
    A[<b>main.c</b><br/>전체 오케스트레이션] --> B[<b>input.c</b><br/>SQL 파일 읽기 + statement 분리]
    B --> C[<b>tokenizer.c</b><br/>문자열을 토큰으로 자르기]
    C --> D[<b>parser.c</b><br/>토큰을 AST로 바꾸기]
    D --> E[<b>executor.c</b><br/>AST 실행]
    E --> F[<b>schema.c</b><br/>테이블 구조 정보 읽기]
    E --> G[<b>storage.c</b><br/>실제 CSV 파일 입출력]
```

```Text
SQL 파일
  -> input.c 에서 파일 읽기
  -> input.c 에서 statement 분리
  -> tokenizer.c 에서 token 생성
  -> parser.c 에서 AST 생성
  -> executor.c 에서 의미 검사 + 실행
  -> schema.c / storage.c 에서 파일 읽기/쓰기
  -> 결과 출력
```


---

## Persistence Note

- Running `./sqlproc --db ./sample_db --file ...` persists `INSERT` results to `./sample_db/public/*.csv`.
- `make demo` now runs against `./sample_db` directly, so repeated runs accumulate rows in `sample_db/public/users.csv`.
- `make check` still uses disposable copies of `tests/fixtures/sample_db_seed/`, so automated verification does not mutate the immutable seed fixture.

## Demo Reset Tip

- If you want to restore the original demo state, reset `sample_db/public/users.csv` before rerunning `make demo`.
# 핵심 개념
## Tokenizer
Tokenizer는 SQL 문장을 작은 조각으로 자르는 단계입니다.

```sql
SELECT id, name FROM users
```

이 문장은 대략 이렇게 나뉩니다.

```text
SELECT / id / , / name / FROM / users
```

이렇게 쪼개 두면 parser가 문법을 훨씬 쉽게 읽을 수 있습니다.


##  AST
AST는 `Abstract Syntax Tree`의 줄임말입니다. 이름이 어려워 보이지만, 사실은 "SQL 문장을 구조체 모양으로 정리해 둔 결과" 라고 생각하면 됩니다.

```sql
SELECT id, name FROM users;
```
는 내부적으로 대략 이런 정보가 됩니다.

```text
종류: SELECT
테이블: users
선택할 컬럼: id, name
```
즉, 원래는 글자였던 SQL이 컴퓨터가 다루기 쉬운 "구조"로 바뀌는 것입니다.


## Executor
Executor는 "이 SQL이 실제로 무슨 일을 해야 하는지" 수행하는 단계입니다.

- `INSERT`면 데이터를 추가합니다.
- `SELECT`면 데이터를 읽고 출력합니다.

여기서 중요한 점은 "문법이 맞는 것"과 "실제로 실행 가능한 것"은 다르다는 것입니다.

- parser는 문법 검사
- executor는 의미 검사 + 실제 실행

---
# 시연

---
# Test Case
| 항목 | 내용 |
| --- | --- |
| 검증 흐름 | `make build` -> `make test` -> `make check` |
| 최종 기준 | `make check`가 end-to-end 최종 게이트 |
| 보호 범위 | parser, storage, executor, error handling, golden output |
| 재현성 | seed fixture를 직접 바꾸지 않고 temporary copy에서만 검증 |
| 통과 증거 | `All tests passed`, `check: all verification steps passed` |

---
# 헙업 방식

이번 수요코딩회는 SQL 처리기 구현과 동시에, **AI 활용 방식 자체를 실험**했습니다.

### 배경
 기존에는 Pair Programming을 통해 Stub Code를 만든 후 기능 별로 나눠 각자 채워가는 방식으로 협업했습니다. 이번엔 AI를 더 잘 활용해서 더 나은 코드를 만들 수 없을지 고민하게 되었습니다.

그 과정에서 하네스 엔지니어링 개념을 접했습니다. AI 세션은 컨텍스트 윈도우가 끊기면 이전 내용을 모릅니다. **하네스 엔지니어링**은 세션 간에 핸드오프 문서를 의도적으로 흘려보내 이 단절을 연결하는 방식입니다.

```
프롬프트 엔지니어링  →  말을 어떻게 걸지
컨텍스트 엔지니어링  →  책상 위에 뭘 올려둘지
하네스 엔지니어링    →  작업장 전체를 설계하는 것
```

### 팀 구조

팀원 4명이 각자 하네스 방식으로 SQL 처리기를 구현하고, 결과물을 함께 비교했습니다. 팀 레벨에서도 하네스 구조가 적용된 셈입니다.

![Team Structure](img/img1.svg)

### 개인 레벨

각자 구현 시에도 하네스를 적용했습니다.
이때 범용 AI 워크플로 스킬 세트를 도입해, AI 세션을 **역할이 아니라 작업 단계** 기준으로 나눴습니다.

> **범용 AI 워크플로 스킬 세트란?**
> AI가 여러 프로젝트에서 반복적으로 잘 일하도록 만드는 공통 작업 루프를 5개 스킬로 표준화한 것입니다.
> 각 스킬은 동일한 입출력 계약(`work-brief`, `repo-context-pack`)을 공유하므로,
> 다음 스킬이 추가 질문 없이 바로 이어서 작업할 수 있습니다.

| 세션 | 스킬 | 하는 일 | 산출물 |
| --- | --- | --- | --- |
| 1 | `request-shaper` | 목표, 범위, 수락 기준, 검증 계획을 정리 | `work-brief` |
| 2 | `repo-context-builder` | 저장소 구조, 빌드 명령, 핵심 경로, 제약을 파악 | `repo-context-pack` |
| 3 | `change-builder` | work-brief + repo-context-pack을 바탕으로 설계·구현·테스트를 한 흐름으로 처리 | 코드 변경 + 테스트 |
| 4 | `quality-guardian` | 코드 리뷰, 회귀 점검, README 작성 | 검증 완료 + 문서 |

각 세션은 이전 세션의 산출물(`work-brief`, `repo-context-pack`, `handoff_to_next_skill`)을 컨텍스트로 받아 시작했습니다.
팀 레벨 하네스 안에 개인 레벨 스킬 체인이 중첩된 구조입니다.

| 기존 방식 (역할 기반) | 스킬 세트 방식 (작업 단계 기반) | 차이 |
| --- | --- | --- |
| PLANNER | request-shaper + repo-context-builder | 작업 정의와 저장소 파악을 분리 |
| GENERATOR A·B·C (함수별 세션 분리) | change-builder 하나로 통합 | 설계와 구현을 한 흐름으로 처리해 컨텍스트 단절 감소 |
| EVALUATOR | quality-guardian | 역할 범위 확장: 리뷰 + 회귀 + 보안 기본 점검 포함 |

---
## 팀 비교 인사이트

같은 하네스 개념을 써도 사람마다 세션 구조, 가정, 설계 결정이 달랐으며, 그 차이가 EVALUATOR 단계에서 드러났습니다. 

![Claude's evaluation](img/img2.png)

#### Codex로 평가한 결과
![Codex's evaluation](img/img3.png)

---
