-- demo.sql
-- Happy path 발표 흐름:
-- 1) INSERT로 새 row를 추가한다.
-- 2) SELECT * 로 전체 column을 읽는다.
-- 3) SELECT id, name 으로 더 작은 projection을 읽는다.
INSERT INTO users (id, name, age) VALUES (3, 'kim', 25), (4, 'lee', 29);
SELECT * FROM users;
SELECT id, name FROM users;
