# 공통 자산 식별과 런타임 재사용

기준일: 2026-10-11. 공통 UUIDv4, 타입별 영구 참조, 인메모리 자산 등록부와 ID 기반 텍스처 재사용을 Windows Debug/Release에서 구현·검증했습니다. FBX 로더, 등록부 파일 포맷, 의존성/빌드 산출물, 런타임 핸들 및 핫 리로드는 아직 구현하지 않았습니다.

## 식별과 타입

[AssetId.hpp](../RavenEngine/Public/Raven/Assets/AssetId.hpp)의 `AssetId`는 표준 순서의 16바이트 UUIDv4입니다. 기본값은 nil이고 `IsValid()`는 false입니다. `Generate()`는 운영체제 난수로 새 UUIDv4를 만들며, 난수 공급에 실패하면 예외를 전달합니다. 시간이나 카운터로 대체하지 않습니다.

`Parse(std::string_view)`는 대소문자를 허용하는 정확한 36자 UUIDv4 문자열을 받습니다. 잘못된 길이·구분자·문자·version·variant와 nil은 `std::invalid_argument`로 거부합니다. UUIDv4만 허용하는 것은 현재 자산 ID 정책입니다. `ToString() const`는 소문자 표준 문자열을 반환하며 nil도 진단용 문자열로 출력할 수 있습니다. ID는 등록 시 한 번 생성하고 이후 같은 값을 사용합니다. 원본의 내용 해시와 자산의 정체성을 혼용하지 않습니다.

[AssetRef.hpp](../RavenEngine/Public/Raven/Assets/AssetRef.hpp)의 `AssetRef<T>`는 UUID를 담는 타입별 값입니다. 로딩이나 자원 소유를 수행하지 않습니다. `TextureAsset`, `ModelAsset`, `MeshAsset`, `MaterialAsset`, `SkeletonAsset`, `AnimationAsset`, `ShaderAsset`, `SceneAsset`은 현재 참조 타입을 구분하는 전방 선언이며 데이터/로더 구현을 의미하지 않습니다. 예를 들어 `AssetRef<MeshAsset>`을 텍스처 로딩에 전달하면 컴파일되지 않습니다. 저장 데이터에서 얻은 ID의 실제 타입은 등록부에서도 확인합니다.

## 등록부

[AssetCatalog.hpp](../RavenEngine/Public/Raven/Assets/AssetCatalog.hpp)는 ID, `AssetKind`, Content 상대 경로를 저장합니다. 공개 컨테이너와 내부 구현을 분리했습니다. 등록·조회·자원 로딩은 현재 애플리케이션/렌더링 스레드에서 수행합니다. 동시 등록이나 비동기 로딩은 지원하지 않습니다. UUID 생성 함수는 별도로 동시 호출을 검증했습니다.

- `Register(const AssetRecord&)`: 일반 자산의 메타데이터를 등록합니다. 텍스처에는 색 공간 설정이 필요하므로 전용 오버로드를 사용합니다.
- `Register(const TextureAssetDesc&)`: 텍스처 경로와 Linear/SRGB 설정을 함께 등록합니다.
- `Get(const AssetId&) const`: 모든 지원 자산 종류의 메타데이터를 조회합니다.
- `GetTexture(AssetRef<TextureAsset>) const`: 등록된 ID의 실제 타입을 확인하고 텍스처 설명을 반환합니다.

등록된 항목은 변경/삭제하지 않습니다. 동일한 내용이어도 중복 ID 등록을 거부합니다. 실패한 등록은 기존 항목을 변경하지 않습니다. 조회 참조는 등록부가 살아 있는 동안 유효하며 이후 다른 항목을 추가해도 유지됩니다. 없는 ID는 `std::out_of_range`, nil ID 및 타입/등록 오류는 `std::invalid_argument`로 보고합니다.

경로는 기존 `Paths::GetContentPath`의 루트/부모 탈출 정책으로 검사하고 상대 형태로 정규화해 저장합니다. 빈 경로, 내장 null 문자와 파일명이 없는 경로도 거부합니다. 물리적 symlink 해결이나 Windows의 대소문자 별칭 병합은 수행하지 않습니다. 등록 시 파일을 읽거나 존재 여부를 검사하지 않으므로 메타데이터 등록과 실제 로딩을 구분합니다.

일반 Model/Mesh/Material 등의 등록은 메타데이터 기능입니다. FBX 내부의 파생 자산 연결, 재임포트 매핑이나 해당 타입의 디코딩은 포함하지 않습니다.

## 런타임 자원 서비스

[AssetResources.hpp](../RavenEngine/Public/Raven/Assets/AssetResources.hpp)의 `AssetResources`는 등록부와 renderer를 빌려 사용하는 동기식 서비스입니다. 생성 시 Content root를 고정하고, `LoadTexture(AssetRef<TextureAsset>)`에서 등록부의 설명을 사용합니다. 게임의 공통 소유자가 하나의 등록부와 자원 서비스를 여러 씬/오브젝트에 공유하도록 합니다.

```cpp
#include "Raven/Assets/AssetCatalog.hpp"
#include "Raven/Assets/AssetResources.hpp"

Raven::AssetCatalog catalog;
const auto id = Raven::AssetId::Parse("7a590cee-ec82-4a27-927f-860e64619c19");
catalog.Register(Raven::TextureAssetDesc
{
	id, "Textures/UVGrid.png", Raven::TextureColorSpace::SRGB
});

Raven::AssetResources resources(catalog, renderer);
const auto& first = resources.LoadTexture({ id });
const auto& second = resources.LoadTexture({ id });
// &first == &second: decoding, upload and descriptor allocation are reused.
```

같은 서비스의 같은 ID는 같은 GPU 텍스처를 반환합니다. 캐시 적중 시 파일을 읽거나 디코딩/업로드하지 않으며, 원본 파일이 삭제되어도 이미 로드된 자원을 반환합니다. 다른 ID가 같은 파일을 가리키는 경우 각각의 논리적 자산으로 취급합니다. 다른 서비스나 renderer context의 캐시는 공유하지 않습니다. Context가 다르면 GPU 자원도 달라야 합니다.

캐시 노드는 GPU 생성 전에 확보합니다. 파일 읽기/디코딩/업로드가 실패하면 해당 노드를 제거하고 예외를 전달해 재시도를 허용합니다. GPU 텍스처와 immutable descriptor는 기존 `VulkanContext`가 계속 소유하며, 반환 참조는 renderer 종료까지 유효합니다. 자원 서비스의 파괴는 GPU 텍스처를 해제하지 않습니다. 등록부와 renderer는 서비스보다 오래 살아야 합니다.

기존 `Images::Load`의 기본 옵션을 사용합니다. 최초 디코딩 성공 시 개발 빌드의 NPOT 경고를 유지하며 캐시 적중 시 반복하지 않습니다. Release에서는 해당 경고를 출력하지 않습니다. 경로/색 공간 설정의 기준은 등록부이며, 로딩 때 다른 설명을 제출해 덮어쓰는 API는 제공하지 않습니다.

## 샘플과 배칭

두 샘플은 공통 `SampleAssets`를 클라이언트 멤버로 소유합니다. 고정 UUID와 경로는 `SampleAssets.cpp`에 모았고 기존 로컬 파일 로딩 함수를 제거했습니다. 샘플 시작마다 새 ID를 생성하지 않습니다. 서비스는 GPU 참조 멤버보다 먼저 초기화되며 클라이언트가 renderer보다 먼저 해제되는 기존 수명 순서를 지킵니다.

렌더러의 기존 생성·드로우 API와 배칭 알고리즘은 유지했습니다. 자산 캐시는 같은 ID의 요청이 같은 텍스처 객체를 사용하게 합니다. 현재 배칭은 같은 메시/텍스처를 사용하는 인접 드로우를 같은 패스에서 묶으며 투명 정렬을 보존합니다. 같은 FBX 파일 ID만으로 한 드로우가 되는 기능을 구현한 것은 아닙니다.

## 플랫폼 연결

UUID layout, parsing과 출력은 공통 C++20 구현입니다. 플랫폼의 유일한 새 연결 지점은 private [PlatformRandom.hpp](../RavenEngine/Private/Platform/PlatformRandom.hpp)입니다.

```cpp
namespace Raven::Platform
{
	void FillRandomBytes(std::span<std::uint8_t> bytes);
}
```

전체 버퍼를 안전한 운영체제 난수로 채우거나 예외를 던져야 합니다. Windows는 `BCryptGenRandom`과 시스템 `bcrypt` 라이브러리를 사용합니다. macOS/Linux 구현과 네이티브 검증은 해당 담당자의 범위이며 여기서 작성하지 않았습니다. 새 hook이 연결되지 않으면 해당 플랫폼의 `AssetId::Generate` 링크가 완료되지 않습니다. [PlatformHandoff.md](PlatformHandoff.md)를 참고합니다.

## 검증

Windows Debug/Release 빌드, 공개 DLL 클라이언트 및 자산 공개 헤더 7개의 독립 `/W4 /WX` 컴파일을 통과했습니다. 새 자산/난수 구현 소스 4개도 `/W4 /WX` 컴파일을 통과했습니다(기존 math 타입의 의도된 alignment padding 경고는 제외). 각 구성에서 3,417개 검사를 통과하고 25개의 실제 프레임을 제출했습니다. 함수/멤버 포인터 타입 검사는 선언·인자·반환·const 계약을 확인하며, 기존 24개 DLL export를 보존하고 자산 관련 export 12개를 추가했습니다.

UUID 1,024개를 4개 스레드에서 생성해 version/variant, 파싱/출력 왕복과 표본의 중복 여부를 확인했습니다. 이는 절대 유일성의 증명이 아닙니다. 난수 공급자 실패를 주입한 별도 임시 probe에서 예외 전달을 확인했습니다. 등록부 검증은 중복/충돌 후 원본 보존, 타입 오류, 잘못된 경로/색 공간, Unicode, 실패 후 재등록과 안정된 조회 참조를 포함합니다.

공개 GPU 클라이언트는 반복 요청, 원본 파일 삭제 후 캐시 사용, 없는/손상된 파일의 복구 후 재시도, 다른 색 공간/ID의 분리, 서비스 파괴 후 renderer 참조 유효성, 서로 다른 renderer context의 자원 분리 및 잘못된 context 드로우 거부를 확인했습니다. Debug API 기록은 의도한 텍스처 자원 8개와 binding 8개만 생성하며, 수백 번의 캐시 적중으로 생성 수가 증가하지 않음을 확인합니다.

배칭 기록은 불투명 257개와 투명 4개가 24프레임 각각 2개의 indexed draw로 제출되고 화면 밖 1개가 제외됨을 확인합니다. 서비스 파괴 후 참조 검증용 1프레임은 별도의 1드로우입니다. Core 및 submit-time synchronization validation 오류/경고는 없으며, Debug의 의도한 NPOT 경고 1회와 Release 0회를 별도로 확인했습니다.

컴퓨터 사용으로 두 실제 Debug 샘플의 텍스처 표시, F1 정렬 전환, 원근 샘플 최대화와 직교 샘플 휠 확대를 확인했습니다. 두 샘플 모두 정상 종료했고 검증 로그가 깨끗합니다. 근거는 `out/validation/assets/`에 있으며, 테스트 코드는 Git에서 제외된 임시 경로에만 있습니다. CTest나 tracked 테스트 파일은 추가하지 않았습니다.
