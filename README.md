📘 Pintos Project: Operating System Internals

KAIST Pintos 프로젝트를 기반으로 운영체제의 스레드, 동기화, 메모리, 파일 시스템을 심층적으로 학습하고 구현한 기록입니다.

🧠 프로젝트 개요

Pintos는 x86 기반의 간단한 교육용 운영체제입니다. 본 프로젝트는 총 4개의 주차 과제로 구성되어 있으며, 운영체제의 핵심 개념들을 직접 구현하면서 이해하는 것을 목표로 합니다.

🔹 Project 1: Threads스레드 관리, 스케줄링, 우선순위 및 기부, 알람 시계 등

🔹 Project 2: User Programs시스템 콜 처리, 프로세스 생성 및 종료, 파일 디스크립터 관리 등

🔹 Project 3: Virtual Memory스택 성장, 페이지 테이블, 스왑 영역 구현 등

🔹 Project 4: File System파일 시스템 구조 설계, 디렉토리 및 inode, 캐시 등

📦 디렉토리 구조 (중요 파일)

├── threads/         # 스레드 및 커널 초기화 코드 (Project 1 핵심)
│   ├── thread.c     # 스레드 생성, 상태 전환, 우선순위 스케줄링
│   ├── synch.c      # 세마포어, 락, 조건변수 등 동기화 도구
│   └── interrupt.c  # 인터럽트 처리 (타이머 등)
│
├── devices/         # 타이머, 키보드, 콘솔, 디스크 등 장치 제어
│   └── timer.c      # 타이머 인터럽트 및 알람 시계 과제 구현 위치
│
├── userprog/        # 사용자 프로그램 실행, 시스템 콜 등 (Project 2)
├── vm/              # 가상 메모리 관련 기능 (Project 3)
├── filesys/         # 파일 시스템 구현 (Project 4)
└── tests/           # 각 주차별 테스트 스위트

✅ Project 1: Threads (1주차 과제)

🔔 Alarm Clock

기존 timer_sleep()은 busy waiting 방식으로 구현되어 있었음

sleep_list를 정렬된 리스트로 유지하며 thread_block()으로 재우고, timer_interrupt() 시각 도달 시 thread_unblock()으로 깨우는 방식으로 개선

🏁 Priority Scheduling

각 스레드는 priority 값을 가지며, 높은 우선순위가 CPU를 선점

락 획득 대기 중 우선순위 역전이 발생하면 priority donation으로 해결

중첩된 도네이션도 지원하며, thread_set_priority() 구현

🔁 Advanced Scheduler (MLFQS)

Nice value, recent_cpu, load_avg 등으로 우선순위 동적 계산

고정소수점 연산 사용 (17.14 형식)

인터럽트 핸들러에서 매 틱마다 적절한 값 갱신

🧩 프로젝트 진행 팁

printf()와 GDB를 적극 활용한 디버깅

thread_block()과 thread_unblock()의 흐름을 정확히 이해할 것

인터럽트 관련 코드는 반드시 intr_disable() / intr_set_level()로 보호

테스트는 make check, make grade로 반복 수행하며 단계적으로 구현

🧪 테스트 예시 (Project 1)

alarm-single, alarm-multiple, alarm-zero, alarm-negative 등 알람 관련 테스트

priority-change, priority-donate-nest, mlfqs-load-1, mlfqs-fair-2 등 스케줄러 관련 테스트

