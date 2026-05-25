# Particle Distribution 구현 정리

## 목적

파티클 모듈의 `float`, `FVector` 설정값을 단순 상수에서 끝내지 않고 언리얼 Cascade 방식처럼 Distribution 시스템으로 승격했다. 이 구조는 값의 authoring 표현과 런타임 평가 표현을 분리한다.

- `UDistribution*`: 에디터/에셋에 저장되는 UObject authoring 계층
- `FRawDistribution*`: 파티클 모듈이 실제 런타임에서 들고 평가하는 경량 wrapper 계층

이 두 층을 함께 둔 이유는 이후 Collision, Event, Curve Editor, Parameter 바인딩이 같은 평가 API를 공유해야 하기 때문이다. 지금 단순 float/vector를 유지하면 Collision을 넣을 때 다시 모든 모듈 필드를 갈아엎어야 하고, 커브/랜덤/파라미터 값을 모듈별로 따로 구현하게 된다.

## 새 파일

- `KraftonEngine/Source/Engine/Distributions/Distributions.h/.cpp`
  - 공통 enum, lookup table, random stream, 보간/매핑 helper
- `KraftonEngine/Source/Engine/Distributions/Distribution.h/.cpp`
  - `UDistribution` base UObject
- `KraftonEngine/Source/Engine/Distributions/DistributionFloat.h/.cpp`
  - float Distribution 계층과 `FRawDistributionFloat`
- `KraftonEngine/Source/Engine/Distributions/DistributionVector.h/.cpp`
  - vector Distribution 계층과 `FRawDistributionVector`

## 공통 계층

### `UDistribution`

모든 Distribution UObject의 base다.

- `Serialize(FArchive&)`: `UPROPERTY(Save)` 기반 저장
- `CanBeBaked()`: raw lookup table로 사전 샘플링 가능한지 여부
- `IsUniform()`: 랜덤 min/max 평가가 필요한지 여부
- `GetTimeRange()`: curve distribution의 샘플링 시간 범위

### `FRawDistribution`

언리얼의 raw layer에 해당한다. 현재는 공통 lookup table을 가진 base 역할이다.

- `FDistributionLookupTable LookupTable`
- `ResetLookupTable()`
- `HasLookupTable()`

현재 파티클 모듈은 안전성을 위해 기본적으로 UObject Distribution을 직접 평가한다. `Initialize()`를 호출하면 bake 가능한 distribution을 lookup table로 샘플링할 수 있게 해 두었다. 이후 LOD compile 단계에서 raw table을 적극 사용하도록 연결하면 된다.

### `FDistributionLookupTable`

런타임 평가용 샘플 테이블이다.

- `Operation`
  - `Uninitialized`
  - `None`
  - `Random`
  - `Extreme`
- `EntryCount`, `EntryStride`
- `TimeScale`, `TimeBias`
- `Values`

float uniform은 stride 2로 min/max를 저장하고, vector uniform은 stride 6으로 min xyz/max xyz를 저장한다.

### `FDistributionRandomStream`

결정적 난수 평가가 필요할 때 쓰는 작은 random stream이다. 지금 API는 `GetValue(..., FDistributionRandomStream*)` 형태로 열어 두었다. null이면 기존처럼 전역 `rand()` 기반으로 동작한다.

## Float Distribution

### `UDistributionFloat`

float distribution base다.

- `GetValue(Time, Data, RandomStream)`
- `GetValueRange(Time, OutMin, OutMax)`
- `GetOutRange(OutMin, OutMax)`

### `FRawDistributionFloat`

파티클 모듈이 들고 있는 float wrapper다.

- `UDistributionFloat* Distribution`
- `GetValue()`
- `GetOutRange()`
- `IsUniform()`
- `Initialize()`

모듈의 `UPROPERTY`는 `Rate.Distribution`처럼 nested member를 직접 가리킨다. 그래서 모듈은 raw wrapper를 들고 있으면서도, 에디터/직렬화는 내부 UObject Distribution을 instanced object로 저장한다.

### 구현된 float subclass

- `UDistributionFloatConstant`
  - `Constant`
- `UDistributionFloatUniform`
  - `Min`, `Max`, `bUseExtremes`
- `UDistributionFloatConstantCurve`
  - `FFloatCurve ConstantCurve`
- `UDistributionFloatUniformCurve`
  - `FFloatCurve MinCurve`
  - `FFloatCurve MaxCurve`
  - `bUseExtremes`
- `UDistributionFloatParameterBase`
  - `ParameterName`
  - `Constant`
  - `MinInput`, `MaxInput`
  - `MinOutput`, `MaxOutput`
  - `ParamMode`
- `UDistributionFloatParticleParameter`
  - `UParticleSystemComponent`의 float parameter를 조회한다.

## Vector Distribution

### `UDistributionVector`

vector distribution base다.

- `GetValue(Time, Data, RandomStream)`
- `GetValueRange(Time, OutMin, OutMax)`
- `GetOutRange(OutMin, OutMax)`

### `FRawDistributionVector`

파티클 모듈이 들고 있는 vector wrapper다.

- `UDistributionVector* Distribution`
- `GetValue()`
- `GetOutRange()`
- `IsUniform()`
- `Initialize()`

### 구현된 vector subclass

- `UDistributionVectorConstant`
  - `Constant`
- `UDistributionVectorUniform`
  - `Min`, `Max`
  - `bUseExtremes`
  - `LockedAxes`
  - `MirrorX`, `MirrorY`, `MirrorZ`
- `UDistributionVectorConstantCurve`
  - `XCurve`, `YCurve`, `ZCurve`
- `UDistributionVectorUniformCurve`
  - `MinXCurve`, `MinYCurve`, `MinZCurve`
  - `MaxXCurve`, `MaxYCurve`, `MaxZCurve`
  - `bUseExtremes`
  - `LockedAxes`
- `UDistributionVectorParameterBase`
  - float parameter base와 같은 mapping 구조를 vector 축별로 적용
- `UDistributionVectorParticleParameter`
  - `UParticleSystemComponent`의 vector parameter를 조회한다.

## Curve 데이터 변경

`FFloatCurve`와 `FCurveKey`를 리플렉션 가능한 `USTRUCT`로 승격했다.

- `FCurveKey`
  - `Time`
  - `Value`
  - `ArriveTangent`
  - `LeaveTangent`
  - `InterpMode`
  - `TangentMode`
- `FFloatCurve`
  - `TArray<FCurveKey> Keys`
  - `PreExtrapMode`
  - `PostExtrapMode`
  - `DefaultValue`

이제 Distribution curve subclass가 curve 데이터를 UObject property로 저장할 수 있다. 별도 Curve Editor는 이 reflected data를 기반으로 붙이면 된다.

## Particle Module 변경

기존 모듈의 raw scalar/vector 필드를 Distribution으로 교체했다.

### Spawn

기존:

- `float Rate`
- `float RateScale`
- `int32 BurstCount`

변경:

- `FRawDistributionFloat Rate`
- `FRawDistributionFloat RateScale`
- `FRawDistributionFloat BurstCount`
- `BurstTime`은 기존 float 유지

`ParticleEmitterInstance::Tick()`에서 매 프레임 `Rate.GetValue()`, `RateScale.GetValue()`를 평가한다. Burst count도 `BurstTime` 기준으로 Distribution을 평가한 뒤 정수화한다.

### Spawn Per Unit

기존:

- `float SpawnPerUnit`

변경:

- `FRawDistributionFloat SpawnPerUnit`

거리 기반 spawn 계산 시 현재 emitter time으로 평가한다.

### Lifetime

기존:

- `MinLifetime`
- `MaxLifetime`

변경:

- `FRawDistributionFloat Lifetime`

기본값은 `UDistributionFloatUniform(1, 1)`이다. Spawn 시 평가한 lifetime을 `OneOverMaxLifetime`에 반영한다.

### Location

기존:

- `StartLocationMin`
- `StartLocationMax`

변경:

- `FRawDistributionVector StartLocation`

기본값은 `UDistributionVectorUniform(Zero, Zero)`이다.

### Velocity

기존:

- `StartVelocityMin`
- `StartVelocityMax`

변경:

- `FRawDistributionVector StartVelocity`

기본값은 `UDistributionVectorUniform((0,0,100), (0,0,100))`이다.

### Color

기존:

- `FVector4 StartColor`
- `FVector4 EndColor`

변경:

- `FRawDistributionVector StartColor`
- `FRawDistributionFloat StartAlpha`
- `FRawDistributionVector EndColor`
- `FRawDistributionFloat EndAlpha`

RGB와 Alpha를 분리했다. 언리얼의 float/vector distribution 계층과 맞추기 위해서다.

### Size

기존:

- `StartSizeMin`
- `StartSizeMax`
- `EndSize`

변경:

- `FRawDistributionVector StartSize`
- `FRawDistributionVector EndSize`

## Parameter 연결

`UParticleSystemComponent`에 parameter storage를 추가했다.

- `TArray<FParticleFloatParameter> FloatParameters`
- `TArray<FParticleVectorParameter> VectorParameters`
- `SetFloatParameter()`
- `SetVectorParameter()`
- `GetFloatParameter()`
- `GetVectorParameter()`

`UDistributionFloatParticleParameter`와 `UDistributionVectorParticleParameter`는 `Data`로 들어온 `UParticleSystemComponent`에서 이름 기반 parameter를 찾는다. `FParticleEmitterInstance::GetDistributionData()`는 이제 component를 반환한다.

## 에디터 처리

Particle System Editor의 property panel에서 Distribution ObjectRef를 인식하도록 연결했다.

- `AllowedClass=UDistributionFloat`
- `AllowedClass=UDistributionVector`

이 경우 dropdown에 concrete Distribution subclass가 표시된다. 선택하면 ObjectFactory로 instanced object를 만들고, 선택된 Distribution UObject의 내부 property를 inline으로 렌더링한다.

현재 Constant/Uniform/Parameter 계열은 바로 편집 가능하다. Curve 계열은 `FFloatCurve`가 저장/반영되는 상태까지 들어갔고, 전용 curve editor UI는 다음 단계에서 붙이면 된다.

## 직렬화 방식

모듈은 `FRawDistribution*`를 실제 member로 가진다. 저장되는 property는 raw wrapper 전체가 아니라 nested UObject pointer다.

예:

```cpp
FRawDistributionFloat Rate;

UPROPERTY(Edit, Save, Instanced, Type=ObjectRef,
          AllowedClass=UDistributionFloat,
          Member=Rate.Distribution,
          CppType=UDistributionFloat*)
;
```

리플렉션 생성기는 이를 `offsetof(UParticleModuleSpawn, Rate.Distribution)`으로 등록한다. `FObjectProperty`의 `PF_InstancedReference` 직렬화를 그대로 쓰기 때문에 저장 포맷은 클래스명과 property payload다. 즉 `UDistributionFloatConstant`로 저장한 값은 로드 시 같은 클래스로 다시 생성된다.

## Collision Module에 주는 의미

Collision Module은 보통 다음 값들을 Distribution으로 받는다.

- damping factor
- damping rotation factor
- max collisions
- delay amount
- collision completion option 관련 threshold

이 값들이 모두 상수라고 가정하면 Collision 자체는 금방 만들 수 있지만, 나중에 curve/parameter/random이 들어오면 충돌 판정과 이벤트 발생 타이밍을 다시 고쳐야 한다. 지금 Distribution이 먼저 들어갔기 때문에 Collision Module은 처음부터 `FRawDistributionFloat/Vector::GetValue()`만 호출하면 된다.

## 남은 다음 단계

- Curve Editor UI를 `FFloatCurve` reflected data에 붙이기
- LOD compile 단계에서 `FRawDistribution::Initialize()` 호출해 lookup table 적극 사용
- Distribution object 교체 시 이전 instanced object cleanup 정책 정리
- Collision Module 구현
- Event Module 구현
