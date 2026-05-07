# JSON Key-Value Store CRUD

[jong-wonPark/json-parsing](https://github.com/jong-wonPark/json-parsing) 라이브러리를 활용해 데이터를 JSON 파일로 영속 관리하는 C 콘솔 CRUD 애플리케이션입니다.

---

## 기능

| 번호 | 기능 | 설명 |
|------|------|------|
| 1 | Read All | 저장된 모든 항목 목록 출력 |
| 2 | Create | 새 Key-Value 항목 추가 (중복 키 방지) |
| 3 | Read One | ID 또는 Key로 단일 항목 조회 |
| 4 | Update Value | 기존 항목의 Value 수정 |
| 5 | Rename Key | 기존 항목의 Key 이름 변경 |
| 6 | Delete | 항목 삭제 (확인 프롬프트) |

---

## 데이터 저장 형식

실행 파일과 같은 디렉터리에 `store.json`으로 저장됩니다.

```json
{
  "store": [
    { "id": 1, "key": "theme",    "value": "dark" },
    { "id": 2, "key": "language", "value": "ko"   }
  ],
  "next_id": 3
}
```

- **id**: 자동 증가 고유 식별자
- **key**: 중복 불가 문자열 키
- **value**: 문자열 값

---

## 프로젝트 구조

```
Project2/
├── Project2.slnx               # Visual Studio 솔루션 파일
└── Project2/
    ├── main.c                  # CRUD 애플리케이션 메인 코드
    ├── json.h                  # JSON 파싱 라이브러리 헤더
    ├── json.c                  # JSON 파싱 라이브러리 구현
    ├── Project2.vcxproj        # Visual Studio 프로젝트 파일
    └── Project2.vcxproj.filters
```

---

## 빌드 환경

| 항목 | 버전 |
|------|------|
| 언어 | C (C11) |
| IDE | Visual Studio 2019 / 2022 |
| Toolset | v145 |
| 플랫폼 | Windows x64 / Win32 |

---

## 빌드 및 실행

1. `Project2.slnx`를 Visual Studio로 열기
2. 빌드 구성 선택 (예: `Debug | x64`)
3. **빌드** (`Ctrl+Shift+B`)
4. **실행** (`Ctrl+F5`)

> 한글 출력을 위해 `/utf-8` 컴파일 옵션이 적용되어 있습니다.

---

## 실행 예시

```
Key-Value Store CRUD 애플리케이션
데이터 파일: store.json

════════ Key-Value Store (JSON CRUD) ════════
 1. 목록 보기      (Read All)
 2. 항목 추가      (Create)
 3. 단일 조회      (Read One)
 4. 값 수정        (Update Value)
 5. 키 이름 변경   (Rename Key)
 6. 항목 삭제      (Delete)
 0. 종료           (Exit)
════════════════════════════════════════════
선택: 2

[추가 - Create]
--------------------------------------------------------
키(Key)   : theme
값(Value) : dark
[완료] ID 1 | key='theme' | value='dark' 저장됨
```

---

## 참고

- JSON 파싱 라이브러리: [jong-wonPark/json-parsing](https://github.com/jong-wonPark/json-parsing)
