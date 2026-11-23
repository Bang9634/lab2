# Lab2 파일 처리 프로세스 및 프로세스 간 통신

리눅스 VM의 기존 Lab2 레포에 `src/`, `Makefile`과 이 안내를 옮긴다. 레포에 이미 `README.md`나 `.gitignore`가 있으면 필요한 내용을 병합한다. 아래 명령은 모두 레포 최상위 디렉터리에서 실행한다. 코드의 실행 파일은 `bin/`, 실습용 파일은 `work/`에 생성한다.

## 준비와 첫 캡처

Ubuntu 또는 Debian에서 실행한다.

```bash
sudo apt update
sudo apt install build-essential git rustc pkg-config libglib2.0-dev dbus-daemon
uname -m
. /etc/os-release; printf '%s\n' "$PRETTY_NAME"
gcc --version | head -n 1
rustc --version
make -j"$(nproc)"
mkdir -p work
```

**그림 1** 환경 정보, 빌드 명령과 컴파일이 끝난 화면. 명령 결과가 화면을 넘으면 환경 정보와 `make` 결과의 마지막 부분을 담는다.

## Rust 행렬 덧셈

```bash
./bin/matrix_add
```

순서대로 두 행렬의 크기를 입력하고 A의 두 행, B의 두 행을 입력한다.

```text
2 3
2 3
1 2 3
4 5 6
6 5 4
3 2 1
```

덧셈 결과는 두 행 모두 `7 7 7`이다. 이어서 크기 불일치도 확인한다.

```bash
printf '2 3\n3 2\n' | ./bin/matrix_add
printf 'exit=%s\n' "$?"
```

**그림 2** 정상 덧셈 결과와 크기 불일치 오류가 담긴 화면. 크기 불일치 종료 코드는 1이다.

## 파일과 디렉터리 함수

```bash
(cd work && ../bin/file_demo)
```

**그림 3** 읽은 문자열, `fstat`의 14바이트, `readdir` 결과와 임시 파일 정리 메시지. 표시 권한은 현재 `umask`의 영향을 받는다.

## 재귀 디렉터리 목록

```bash
tree=$(mktemp -d -p work tree.XXXXXX)
mkdir -p "$tree/a/deep" "$tree/b"
touch "$tree/top.txt" "$tree/a/note.txt" "$tree/.hidden"
ln -s .. "$tree/a/back"
LC_ALL=C ./bin/recursive_ls "$tree" | tee work/recursive.txt
LC_ALL=C ls -R --color=never --quoting-style=literal "$tree" > work/gnu-ls.txt
diff -u work/gnu-ls.txt work/recursive.txt && echo 'ls -R comparison: MATCH'
```

**그림 4** 하위 디렉터리 목록과 `MATCH`가 나온 화면. 구현은 하나의 디렉터리 인자, 기본 숨김 파일 제외, 로캘 정렬, 한 줄에 항목 하나인 출력에 맞춘다. 심볼릭 링크인 하위 항목은 이름을 표시하고 재귀 방문을 건너뛴다.

## 타자 연습

```bash
./bin/typing
```

줄바꿈을 제외한 ASCII 입력을 대상으로 한다. 첫 문장을 아래처럼 `s` 하나를 빼고 타이핑하고, 나머지 문장을 화면과 동일하게 입력한다.

```text
Linux files have permission.
A pipe connects two processes.
Shared memory needs synchronization.
```

**그림 5** 문장별 오타, 합계, 경과 시간, `Average CPM`이 보이는 화면. 오타는 제출된 줄의 삽입·삭제·치환 최소 횟수다. CPM은 공백과 문장부호를 포함한 입력 문자 수 × 60 ÷ 각 문장 입력 시간의 합이며, 문자 수에서 Enter를 제외하고, 시간은 문장이 표시된 직후부터 Enter가 입력될 때까지 잰다. Backspace로 Enter 전에 수정한 이력은 오타 집계에 들어가지 않는다.

## 프로세스 시그널과 FIFO

```bash
./bin/process_signal
(cd work && ../bin/fifo_demo)
```

**그림 6** 부모가 자식에게 보낸 `SIGUSR1`, 자식의 `exec` 출력, 부모의 `waitpid` 종료 코드.

**그림 7** FIFO가 생성되고 자식이 메시지를 수신한 뒤 FIFO가 정리되는 화면. 그림 6과 7은 한 이미지로 보내도 된다.

## 익명 파이프 채팅

```bash
./bin/pipe_chat
```

한 터미널에서 프롬프트가 A와 B로 번갈아 표시된다. A가 `Hello from A`, B가 `Hello from B`, A가 `/quit`를 입력한다. A와 B는 별도의 프로세스이며 각 방향에 파이프 하나를 사용한다.

**그림 8** 양방향 입력·수신과 종료가 보이는 화면. 메시지 한 줄은 254바이트까지 입력한다.

## POSIX 메시지 큐 채팅

같은 VM에서 같은 사용자로 터미널을 두 개 연다. 아래 예제에서는 양쪽에 같은 방 이름 `run01`을 사용한다.

```bash
# 터미널 A. Queues ready가 나올 때까지 기다린다.
./bin/mq_chat A run01
```

```bash
# 터미널 B
./bin/mq_chat B run01
```

A에서 `Message to B`를, B에서 `Reply to A`를 보낸 뒤 A에 `/quit`를 입력한다. 메시지는 번갈아 전송하고 한 줄에 254바이트까지 입력한다. A가 큐를 생성하고 정상 종료 시 두 이름을 삭제한다. 강제 종료로 같은 방이 남아 있으면 다음 실습에서 새 방 이름을 양쪽에 동일하게 지정한다.

**그림 9** 두 터미널의 송신과 수신을 한 화면에 배치하거나 A와 B를 따로 캡처한다.

## POSIX 공유 메모리로 파일 복사

```bash
dd if=/dev/urandom of=work/original.bin bs=65536 count=3 status=none
./bin/shm_copy work/original.bin work/copied.bin
cmp work/original.bin work/copied.bin && echo 'Binary copy: MATCH'
sha256sum work/original.bin work/copied.bin
: > work/empty.bin
./bin/shm_copy work/empty.bin work/empty-copy.bin
cmp work/empty.bin work/empty-copy.bin && echo 'Empty copy: MATCH'
```

**그림 10** 복사 출력, 동일한 체크섬, 두 `MATCH` 메시지. 프로그램은 원본과 대상에 일반 파일을 사용하고, 64 KiB 크기의 공유 영역을 부모와 자식이 번갈아 사용한다. 대상과 같은 디렉터리에 만든 임시 파일을 전송 성공 시 교체하며, 결과 파일은 0600으로 생성된다.

## D-Bus 알림 서버와 클라이언트

SSH 터미널에서도 서버와 클라이언트가 같은 세션 버스를 사용하도록 새 버스 안에서 실행한다.

```bash
dbus-run-session -- bash
./bin/notify_server &
server_pid=$!
# Listening on org.lab2.Notification 출력 후 실행한다.
./bin/notify_client 'Lab2 notification test'
kill -TERM "$server_pid"
wait "$server_pid"
exit
```

**그림 11** 서버의 `Notification: Lab2 notification test`와 클라이언트의 `Server reply: true`가 같이 보이는 화면. `Notify`는 문자열을 받아 서버 터미널에 표시하고 불리언 응답을 반환하는 세션 버스 메서드다.

## 팀 과제 간단한 셸

실제 터미널에서 `./bin/mini_shell`을 실행한다. 아래 명령은 표시된 `lab2$` 프롬프트 안에서 순서대로 입력한다. `work/lab2-shell-demo`가 이미 있다면 새 이름을 사용한다.

```text
mkdir work/lab2-shell-demo
cd work/lab2-shell-demo
pwd
printf 'alpha\nbeta\n' > input.txt
cat < input.txt | grep '^beta$' > result.txt
cat result.txt
cp result.txt copied.txt
ln copied.txt linked.txt
mv linked.txt moved.txt
ls
rm moved.txt
rm copied.txt
rm result.txt
rm input.txt
cd ..
rmdir lab2-shell-demo
sleep 3 &
jobs
sleep 30
```

마지막 `sleep 30` 중 **Ctrl-C**를 눌러 프롬프트로 돌아온다. 다시 `sleep 30 | cat`을 실행하고 다음을 확인한다.

1. **Ctrl-Z**로 전경 파이프라인을 정지하고 `jobs`에서 `Stopped` 상태를 확인한다.
2. `fg 번호`로 재개한 뒤 **Ctrl-C**로 종료한다. 예를 들어 목록이 `[1] Stopped`라면 `fg 1`을 입력한다.
3. `exit`로 셸을 종료한다. **Ctrl-\**는 별도의 `SIGQUIT`이며 검증할 경우 `ulimit -c 0`을 적용한 Bash에서 셸을 다시 실행해 전경 `sleep 30`에 눌러 볼 수 있다.

**그림 13** `beta` 출력, 경로 변화와 파일 명령 결과. **그림 14** `Background`, Ctrl-C, Ctrl-Z, `Stopped`, `fg` 이후 종료. 작업 번호나 PID는 실행마다 다를 수 있다.

기본 명령 11개는 `src/shell_commands.c`와 `src/list_dir.c`에 구현했다. 그 외 일반 프로그램은 `execvp`로 실행한다. 셸은 기본 따옴표와 백슬래시, `<`, `>`, `|`, 줄 끝의 `&`를 지원한다. 변수·글로브 확장, 명령 치환, `>>`, `&&`는 범위 밖이다. `ln`은 하드 링크, `mv`는 동일 파일시스템 내 이름 변경, `grep`은 기본 정규식, `ls`는 디렉터리와 선택적 `-R`을 지원한다. 팀 분담 예시는 디렉터리 5개(`ls pwd cd mkdir rmdir`), 파일 4개(`ln cp rm mv`), 입출력 2개(`cat grep`)와 통합 검증이다. 실제 팀 인원과 작성·검토 범위에 맞춰 배분하고 보고서에 기록한다.

## 로컬 커밋과 보고서 반영

기존 레포에서 커밋 전 변경 목록과 현재 브랜치를 확인한다. 이번 작업은 로컬 커밋까지 진행한다.

```bash
git status --short
git branch --show-current
git add -- src Makefile .gitignore README.md
git diff --cached --stat
git commit -m "Implement Lab2 Linux systems programming exercises"
git log -1 --oneline
```

**그림 12** 로컬 Git 커밋 이력과 작업 트리 상태. 최종 제출 전 보고서의 제출일, 실행 환경, 결과 사진, 커밋 주소, 팀 분담과 실제 검토 소감을 채운다.
