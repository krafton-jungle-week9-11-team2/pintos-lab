# Pintos Project: Operating System Internals

> KAIST Pintos 프로젝트를 기반으로 운영체제의 스레드, 동기화, 메모리, 파일 시스템을 심층적으로 학습하고 구현한 기록입니다.

---

## 프로젝트 개요

**Pintos**는 x86 기반의 간단한 교육용 운영체제입니다. 본 프로젝트는 총 4개의 주차 과제로 구성되어 있으며, 운영체제의 핵심 개념들을 직접 구현하면서 이해하는 것을 목표로 합니다.

* **Project 1: Threads**
  스레드 관리, 스케줄링, 우선순위 및 기부, 알람 시계 등

* **Project 2: User Programs**
  시스템 콜 처리, 프로세스 생성 및 종료, 파일 디스크립터 관리 등

* **Project 3: Virtual Memory**
  스택 성장, 페이지 테이블, 스왑 영역 구현 등

* **Project 4: File System**
  파일 시스템 구조 설계, 디렉토리 및 inode, 캐시 등

---

## 디렉토리 구조 (중요 파일)

```
├── threads/         스레드 및 커널 초기화 코드 (Project 1 핵심)
│   ├── thread.c     스레드 생성, 상태 전환, 우선순위 스케줄링
│   ├── synch.c      세마포어, 락, 조건변수 등 동기화 도구
│   └── interrupt.c  인터럽트 처리 (타이머 등)
│
├── devices/         타이머, 키보드, 콘솔, 디스크 등 장치 제어
│   └── timer.c      타이머 인터럽트 및 알람 시계 과제 구현 위치
│
├── userprog/        사용자 프로그램 실행, 시스템 콜 등 (Project 2)
├── vm/              가상 메모리 관련 기능 (Project 3)
├── filesys/         파일 시스템 구현 (Project 4)
└── tests/           각 주차별 테스트 스위트
```

---

## Project 1: Threads (1주차 과제)

### Alarm Clock

* 기존 `timer_sleep()`은 busy waiting 방식으로 구현되어 있어 CPU를 낭비함
* 개선 방식은 `sleep_list`를 정렬된 리스트로 유지하고 `thread_block()`으로 스레드를 재운 뒤,
  `timer_interrupt()` 시각 도달 시 `thread_unblock()`으로 깨우는 방식

### Priority Scheduling

* 각 스레드는 `priority` 값을 가지며, 높은 우선순위가 CPU를 선점함
* 락을 기다리는 동안 우선순위 역전이 발생할 경우 **priority donation**으로 우선순위가 전파됨
* 중첩된 도네이션도 지원하며, `thread_set_priority()`를 통해 우선순위 갱신 가능

### Advanced Scheduler (MLFQS)

* nice 값, recent\_cpu, load\_avg 등으로 우선순위를 동적으로 계산함
* 고정소수점 연산 (17.14 형식)을 사용해 정밀한 계산 수행
* 타이머 인터럽트마다 recent\_cpu 증가 및 주기적으로 load\_avg 갱신

---

## 프로젝트 진행 팁

* `printf()`와 `GDB`를 적극 활용해 디버깅하며 흐름을 시각화할 것
* `thread_block()`과 `thread_unblock()`의 동작 흐름을 명확히 이해할 것
* 인터럽트 관련 코드는 반드시 `intr_disable()` / `intr_set_level()`로 보호
* 테스트는 `make check`, `make grade` 등으로 반복 수행하며 안정성 확보

---

## 테스트 예시 (Project 1)

* **알람 관련**: `alarm-single`, `alarm-multiple`, `alarm-zero`, `alarm-negative`
* **스케줄러 관련**: `priority-change`, `priority-donate-nest`, `mlfqs-load-1`, `mlfqs-fair-2`


---

## 참고 문서

* Pintos 공식 문서 (KAIST 버전)
* CS\:APP (Computer Systems: A Programmer’s Perspective)
* MIT 6.828 운영체제 자료

---
