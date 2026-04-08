-- error.sql
-- 존재하지 않는 column을 요청해 executor 단계의 semantic error를 보여 준다.
SELECT nickname FROM users;
