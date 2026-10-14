# 스프라이트 표현과 배칭 비교 조사

조사일: 2026-10-11. 구현 변경 없이 현재 RavenEngine 코드, 공식 문서와 공개 엔진 소스를 대조한 설계 검토다.

## 조사 범위와 결론

상용 엔진은 Unity, Unreal Engine의 Paper2D, GameMaker를 비교했다. 내부 구현까지 확인할 수 있는 비교 대상으로 Godot과 Defold도 조사했다. BlackSpace는 펄어비스와 PlayStation의 공개 자료에서 확인할 수 있는 범위를 구분했다. 모든 비공개 엔진의 내부 구현을 확인했다는 의미는 아니다.

Unity는 6.0 문서, Godot은 `4.6-stable`의 RD 렌더러 소스를 기준으로 한다. Unreal은 조사 시 제공되는 Paper2D 공식 문서/API, GameMaker는 monthly 매뉴얼을 사용했다. Defold의 구현 설명은 조사일에 읽은 `dev` 브랜치 소스에 한정하며, 모든 배포 버전의 동일 동작을 보장하지 않는다.

**스프라이트 자산, 화면에 제출하는 개별 스프라이트, GPU 배치는 서로 다른 책임이다.** 스프라이트를 사각형으로 표현할 수 있다는 사실만으로 모든 스프라이트가 같은 GPU 메시를 공유하거나 한 드로우 콜로 그려진다고 결론 내릴 수 없다.

이전 설명에서 샘플의 사각형 공유를 엔진 전체 구조의 근거로 사용한 부분은 잘못된 일반화였다. 조사 결과 Unity, Unreal, Defold는 사각형 외의 스프라이트 지오메트리도 지원한다. 사각형 인스턴싱은 유효한 최적화 경로이지만 스프라이트 전체의 정의가 될 필요는 없다.

## 엔진별 확인 결과

| 엔진 | 스프라이트 표현 | 배칭에 대한 확인 내용 | 설계에서 주의할 점 |
| --- | --- | --- | --- |
| Unity | `Full Rect` 사각형, 알파 윤곽을 따르는 `Tight` 메시, 사용자 윤곽을 지원한다. | Sprite Atlas로 여러 이미지를 같은 텍스처에 모은다. SRP Batcher는 드로우 사이 상태 설정의 CPU 비용을 줄이는 별도 최적화다. | 아틀라스 사용이나 SRP 배치라는 이름만으로 모든 스프라이트가 GPU 인스턴싱 또는 한 드로우로 합쳐진다고 판단하면 안 된다. |
| Unreal Paper2D | 스프라이트 자산에 렌더 지오메트리가 있으며 사각형, 윤곽, 사용자 형상, Diced 등을 지원한다. | 별도 `UPaperGroupedSpriteComponent`가 여러 스프라이트 자산의 인스턴스를 다룬다. `UPaperSpriteAtlas`는 공유 텍스처를 통해 드로우 결합을 가능하게 한다. | 그룹 액터는 그룹 전체를 한 단위로 렌더·컬링한다. 공개 API의 instance라는 명칭만으로 GPU 하드웨어 인스턴싱을 확정할 수 없다. |
| GameMaker | 개발자는 스프라이트를 그리도록 요청하고, 엔진이 이미지들을 Texture Page에 배치한다. | 공식 매뉴얼은 버텍스 배치를 설명한다. 블렌드·셰이더·셰이더 상수 변경과 일부 도형 그리기 등은 배치를 끊는다. | 개발자가 공유 사각형 메시를 직접 만들 필요는 없다. 텍스처 페이지 구성과 상태 변경이 배치 효율에 영향을 준다. |
| Godot | 2D 렌더러가 사각형, nine-patch, 폴리곤, 메시 등의 명령을 구분한다. | 확인한 RD 소스의 사각형 경로는 개별 색·변환·영역 데이터를 인스턴스 버퍼에 넣고, 배치의 인스턴스 수로 그린다. 폴리곤 명령은 별도 드로우 경로다. | 사각형에 최적화한 인스턴싱을 제공하면서 다른 형상 경로를 유지할 수 있다. 텍스처·머티리얼·클립 등의 차이는 배치를 나눈다. |
| Defold | 기본 사각형 외에 4~8개 정점의 윤곽 트리밍과 slice-9를 지원한다. | 확인한 소스는 배치의 정점·인덱스 데이터를 CPU에서 공용 버퍼에 작성한다. 문서는 정렬 후 이웃한 호환 항목을 한 드로우로 묶는 조건을 정의한다. | 동일한 스프라이트 메시를 전제하지 않는 버텍스 배칭 사례다. 기본 tint처럼 셰이더 상수로 제공되는 값의 차이도 배치를 끊을 수 있다. |
| BlackSpace | 이번 조사에서 스프라이트 자산/API 구조를 확인할 공개 근거를 찾지 못했다. | PS5의 NGG Culling과 Geometry Shader Oversubscription 활용은 공식 PlayStation 자료에서 확인된다. | 이 사실은 스프라이트 공유 메시, 버텍스 배칭, 인스턴싱, 아틀라스 또는 투명 정렬 방식의 증거가 아니다. |

### Unity

스프라이트의 모양은 임포트 설정의 일부다. `Full Rect`는 사각형을 만들고, `Tight`는 픽셀 알파에 맞춘 메시를 만든다. 따라서 자산의 의미와 GPU 메시 공유 여부를 분리해야 한다. [공식 Sprite Import Settings](https://docs.unity3d.com/6000.0/Documentation/Manual/texture-type-sprite.html)

Sprite Atlas는 여러 이미지가 하나의 텍스처를 사용하도록 해 드로우 결합 기회를 늘린다. 공식 문서는 SRP Batcher 사용 시 드로우 수가 줄지 않아도 성능이 개선될 수 있음을 명시한다. [공식 Sprite Atlas 설명](https://docs.unity3d.com/6000.0/Documentation/Manual/sprite/atlas/atlas-introduction.html)

SRP Batcher는 호환 셰이더의 드로우 사이 상태 설정 비용을 줄인다. 이를 정점 결합이나 GPU 인스턴싱과 동일한 기능으로 설명하면 안 된다. 또한 6.0.0b11 릴리스 노트에는 URP Sprite Renderer의 비-SRP 호환 머티리얼에 대한 dynamic batching 수정이 기록돼 있다. 이는 경로가 조건에 따라 달라지는 사례이며, 모든 Sprite Renderer의 내부 알고리즘을 확정하는 근거는 아니다. [SRP Batcher](https://docs.unity3d.com/6000.0/Documentation/Manual/SRPBatcher.html), [6.0.0b11 릴리스 노트](https://unity.com/releases/editor/beta/6000.0.0b11)

### Unreal Paper2D

에디터는 `Source Bounding Box`, `Tight Bounding Box`, `Shrink Wrapped`, `Fully Custom`, `Diced` 렌더 형상을 다룬다. 자산 API의 `BakedRenderData`는 위치·UV로 구성된 삼각형 정점을 보관한다. 모든 스프라이트를 같은 사각형 메시로 제한하는 구조가 아니다. [Sprite Editor](https://dev.epicgames.com/documentation/en-us/unreal-engine/paper-2d-sprite-editor-in-unreal-engine), [UPaperSprite](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Paper2D/UPaperSprite)

그룹 컴포넌트에는 스프라이트 자산·변환·색·머티리얼을 가진 인스턴스를 추가할 수 있다. 그룹 액터의 공식 설명은 그룹 전체를 하나의 렌더·컬링 단위로 다룬다고 명시한다. 이를 그대로 크게 묶으면 개별 스프라이트 단위의 컬링 정밀도를 잃을 수 있다는 것은 이 계약에서 도출되는 설계상 비용이다. [Grouped Component](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Paper2D/UPaperGroupedSpriteComponent), [Grouped Actor](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Paper2D/APaperGroupedSpriteActor)

Atlas API는 공유 텍스처를 통해 드로우 결합을 가능하게 한다. 조사 시 API는 해당 클래스를 Experimental로 표기한다. 공개 API만으로 그룹의 모든 내부 GPU 제출 방식이나 일반 Sprite Component의 자동 배칭 범위를 확정하지 않았다. [UPaperSpriteAtlas](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Paper2D/UPaperSpriteAtlas)

### GameMaker

엔진은 스프라이트·타일·폰트 이미지를 Texture Page로 묶고, 기본적으로 이미지 주변의 투명한 빈 영역을 잘라 패킹한다. 이는 이미지 저장 영역을 줄이는 작업이며 Unity의 Tight 폴리곤 메시와 동일하다는 의미는 아니다. [Texture Pages](https://manual.gamemaker.io/monthly/en/Settings/Texture_Information/Texture_Pages.htm)

매뉴얼은 텍스처 전환과 버텍스 배치를 별도로 설명한다. 여러 그리기 요청의 정점 데이터를 배치로 보내되, 블렌드나 셰이더 및 셰이더 상수 변경 등에서 배치가 끊어진다. 이 공개 설명에서 모든 백엔드의 저수준 호출 방식까지 추정하지 않았다. [Texture Swaps and Vertex Batches](https://manual.gamemaker.io/monthly/en/Additional_Information/Best_Practices_When_Programming.htm)

### Godot

`4.6-stable`의 `RendererCanvasRenderRD`에서 확인한 내용이다. 사각형·nine-patch는 인스턴스 버퍼와 공용 사각형 인덱스를 사용한다. 사각형마다 변환·색·소스 UV 영역·목적 영역을 기록하며 배치의 `instance_count`를 드로우에 전달한다. 반면 폴리곤은 자체 정점·인덱스 버퍼로 별도 제출한다. 머티리얼, 텍스처/필터/반복 설정, 클립, 명령 종류 등의 차이로 새 배치를 만든다. 이는 사각형 전용 최적화와 일반 형상을 함께 지원하는 실제 구현 사례다. 이 조사로 다른 버전이나 Compatibility 백엔드의 동일 구현을 주장하지 않는다. [공식 RD 렌더러 소스](https://github.com/godotengine/godot/blob/4.6-stable/servers/rendering/renderer_rd/renderer_canvas_render_rd.cpp)

### Defold

아틀라스의 Sprite Trim Mode는 기본 사각형을 유지하거나 투명한 영역을 줄이는 4~8 정점 형상을 선택하게 한다. 트리밍과 slice-9는 함께 사용할 수 없다. [Atlas의 Image Properties](https://defold.com/manuals/atlas/)

문서는 정렬된 목록에서 이웃한 항목이 같은 collection proxy·컴포넌트 종류·텍스처·머티리얼·셰이더 상수를 사용해야 한 드로우로 묶인다고 설명한다. tint가 상수로 구현된 경우 값이 다르면 배치가 갈라진다. [Draw Calls and Batching](https://defold.com/manuals/render/)

조사일의 `dev` 소스에서 `RenderBatch`는 `CreateVertexData`로 공용 정점·인덱스 데이터를 작성하고 동적 버퍼에 업로드한다. 사각형과 트리밍된 형상은 서로 다른 정점 수를 가질 수 있다. 렌더 리스트에는 스프라이트별 가시성 콜백이 등록되며, 해당 콜백은 경계 구와 프러스텀의 교차 여부를 검사한다. 배칭을 위해 반드시 그룹 전체만 컬링해야 하는 것은 아니라는 구현 사례다. [공식 스프라이트 컴포넌트 소스](https://github.com/defold/defold/blob/dev/engine/gamesys/src/gamesys/components/comp_sprite.cpp)

## BlackSpace: 확인된 사실과 미확인 항목

펄어비스의 2025-03-28 GDC 소개는 물, 유체 기반 볼류메트릭 안개, 대기 산란, GPU 천·머리카락 시뮬레이션, 레이트레이싱을 공개한다. 스프라이트 자산의 형상이나 배치 키, 인스턴스 데이터 배치를 설명하는 기술 문서는 아니다. [펄어비스 Dev Archives](https://crimsondesert.pearlabyss.com/en-us/News/Notice/Detail?_boardNo=40)

2026-03-04 PlayStation 자료는 펄어비스의 PS5 최적화에 NGG Culling과 Geometry Shader Oversubscription을 사용한다고 전한다. 적용 플랫폼과 기술 이름은 확인할 수 있지만, 스프라이트 배칭 방식은 설명하지 않는다. [공식 PS5 기술 소개](https://blog.playstation.com/2026/03/04/crimson-desert-hands-on-report-four-hours-in-the-rpgs-massive-open-world/)

| 질문 | 조사 결과 |
| --- | --- |
| 엔진 공용 사각형으로 모든 스프라이트를 표현하는가? | 확인할 공개 근거를 찾지 못했다. |
| 스프라이트를 버텍스 결합 또는 GPU 인스턴싱으로 묶는가? | 확인할 공개 근거를 찾지 못했다. |
| 어떤 텍스처·머티리얼 조건으로 배치를 나누는가? | 미확인. |
| 투명 스프라이트의 정렬 또는 OIT 알고리즘은 무엇인가? | 미확인. |
| PS5의 지오메트리 컬링 기술은 공개됐는가? | NGG Culling 활용은 확인됨. 스프라이트별 컬링 구조는 미확인. |

검색된 제3자 분석이나 익명 게시물의 Nanite 유사 클러스터, 특정 인스턴싱 구조, 버텍스 삭제 등의 주장은 검증 근거로 채택하지 않았다. 예전 검은사막 엔진 자료도 BlackSpace의 동일 구현을 입증하는 자료로 사용하지 않았다. 미확인은 기능 부재라는 의미가 아니다.

## 구현 방식 선택의 장단점

아래는 조사 사례를 Raven에 적용하기 위한 설계 판단이다. 특정 엔진의 성능 수치를 측정한 결과는 아니다.

| 방식 | 장점 | 비용 또는 제한 |
| --- | --- | --- |
| 동일 메시의 GPU 인스턴싱 | 반복 메시의 정점을 재작성하지 않고 변환·색 등 개별 데이터를 모을 수 있다. 사각형 외에도 같은 메시 자원이면 가능하다. | 단순한 인스턴스 드로우는 동일 지오메트리가 필요하다. 서로 다른 메시를 같은 머티리얼이라는 이유만으로 합칠 수 없다. 인스턴스 데이터 업로드와 프레임별 수명 관리도 필요하다. |
| 동적 버텍스·인덱스 배칭 | 서로 다른 스프라이트 형상도 같은 정점 형식과 렌더 상태라면 공용 버퍼 구간에 모을 수 있다. | CPU 정점 작성/변환, 업로드량, 인덱스 범위와 버퍼 용량 관리 비용이 든다. 현재 Raven의 GPU 전용 메시 소유 구조에 바로 덧붙이기 어렵다. |
| 상태 설정 비용 감소 | 지오메트리를 합치기 어려운 드로우에도 적용할 수 있다. | 드로우 수 자체가 줄어드는 것은 아니다. 인스턴싱·버텍스 배칭과 효과를 구분해 측정해야 한다. |
| Texture Atlas | 다른 원본 이미지가 같은 GPU 텍스처를 쓰게 해 배치 호환성을 높인다. | 패딩·필터링·밉 경계·패킹·페이지 수명·리로드를 관리해야 한다. 정렬이나 다른 렌더 상태의 차이까지 제거해 주지는 않는다. |

사각형은 정점이 적고 인스턴싱이 단순하다. 알파 윤곽 형상은 투명한 영역의 프래그먼트 처리를 줄일 여지가 있지만 정점 수와 자산 처리 비용이 늘어난다. 어느 쪽이 빠른지는 CPU 드로우 준비 비용과 GPU 픽셀 처리량을 측정해 판단해야 한다. 배칭으로 드로우 수가 줄어도 겹쳐 그리는 픽셀 수가 자동으로 줄어들지는 않는다.

## 현재 Raven 코드와 권장 방향

현재 `VulkanTexturedDraw`는 임의의 `VulkanTexturedMesh`와 `VulkanTexture`를 참조한다. `CreateTexturedMesh`도 호출자가 넘긴 삼각형 데이터를 받아들인다. `OrthographicSample`이 `m_Quad`를 공유하는 것은 샘플의 선택이다.

현행 큐는 제출별 메시 경계로 프러스텀을 검사한다. 투명 항목은 `SortLayer`, `SortOrder`, 카메라 깊이, 제출 순서로 정렬하며, 렌더 패스는 각 항목을 `instanceCount = 1`로 그린다. 이 구현에 모든 메시가 사각형이라는 계약은 없다.

권장 책임 분리는 다음과 같다.

| 책임 | 소유하는 정보 |
| --- | --- |
| 스프라이트 자산 | 이미지/텍스처 영역, 크기, 피벗, 필요시 렌더 형상. 자산 식별 체계와 함께 설계한다. |
| 개별 렌더 제출 | 자산 참조, 변환, tint, 레이어·정렬값. 게임 코드는 GPU 버퍼 배치 방식에 의존하지 않는다. |
| 렌더러 | 가시성 선택, 순서 확정, 호환 배치 생성, 프레임별 GPU 데이터, 실제 드로우. |

현재 단계에서 가장 작은 구현 후보는 **기존 큐에서 같은 메시·텍스처·파이프라인을 쓰는 연속 항목을 인스턴싱하는 것**이다. 이 방법은 임의 메시 지원을 유지하며 모든 스프라이트에 메시 공유를 강요하지 않는다. 변환과 tint는 개별 인스턴스 데이터로 옮겨 서로 다른 색도 같은 배치에서 처리할 수 있게 한다. 다른 메시 자원은 분리하므로 서로 다른 스프라이트 형상을 자동으로 결합하는 완성형 Sprite Batcher와는 범위가 다르다.

추후 전용 스프라이트 자산/API를 도입한다면 사각형의 내부 공용 지오메트리와 UV 영역을 이용하는 경로를 둘 수 있다. 윤곽 또는 변형 형상은 기존 메시 경로를 사용하고, 측정상 필요한 경우 버텍스 배칭을 추가한다. 전용 API 설계에서도 모든 스프라이트 형상을 사각형으로 고정하는 전제는 피한다.

처리 순서는 **개별 가시성 검사 → 렌더 순서 확정 → 연속한 호환 항목 묶기 → GPU 제출**로 유지하는 것을 권장한다. 투명 순서가 `A, A, B, A`이고 A/B가 서로 다른 텍스처라면 기본 배치는 `[A,A] [B] [A]`다. 마지막 A를 앞으로 옮겨 드로우를 줄이면 겹침 결과가 바뀔 수 있다. 아틀라스가 같은 텍스처를 제공하고 나머지 상태도 호환되면, 이 순서를 유지한 채 더 크게 묶을 수 있다.

큰 영구 그룹을 먼저 만들어 그룹 경계만 컬링하는 방식은 초기 Raven의 기본 경로로 권장하지 않는다. 현재의 개별 가시성 선택을 유지하고 살아남은 항목을 프레임 내에서 묶으면 배칭과 개별 컬링을 함께 유지할 수 있다. CPU 컬링 비용이 실제 병목이 되면 공간 분할이나 GPU 가시성 경로를 별도로 검토한다.

배칭의 검증은 화면이 기존과 같고, 호환 반복 제출에서 실제 드로우 수가 줄며, 다른 텍스처·메시·투명 순서와 화면 밖 항목을 올바르게 처리하는지 확인해야 한다. 현재 두 텍스처를 사용하는 소규모 샘플만으로 일반적인 배칭 성능 향상을 입증할 수는 없다.

이 문서는 조사와 구현 후보만 정리한다. 엔진 구현, 로드맵 및 TODO의 완료 상태는 변경하지 않았다.
