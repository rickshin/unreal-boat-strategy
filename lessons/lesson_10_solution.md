# Lesson 10 — Solutions

**10.1 — Acquire a ship.**
After importing in the editor:

```bash
ls Content/Models/
# ship_dark.uasset   (or whatever yours is called)
```

Kenney's packs ship with a `License.txt` reading "Creative Commons Zero
(CC0)" — you may use, modify, sell, and redistribute, credit optional
(but kind). If you grabbed a CC-BY model instead, add a credit line to
the README before pushing. If you grabbed a Fab marketplace asset:
using it in the game is fine, but pushing the asset to a public repo
usually is **not** — keep the repo private or keep the asset out.

**10.2 — The full pipeline.**

*Schema* — `Sim/SimContent.h`, in `FUnitTpl`:

```cpp
	float RepairRate = 0.f;   // hp/sec (repair vessels)
	// Optional custom model (render-only; the sim never reads these).
	FString MeshPath;         // e.g. "/Game/Models/ship_dark.ship_dark"
	float MeshScale = 1.f;
	float MeshYawDeg = 0.f;
```

*Parser* — `Sim/SimContent.cpp`, in the unit loop beside the other
optional fields:

```cpp
	T.MeshPath = U->HasField(TEXT("mesh")) ? U->GetStringField(TEXT("mesh")) : FString();
	T.MeshScale = U->HasField(TEXT("meshScale")) ? U->GetNumberField(TEXT("meshScale")) : 1.0;
	T.MeshYawDeg = U->HasField(TEXT("meshYaw")) ? U->GetNumberField(TEXT("meshYaw")) : 0.0;
```

*Renderer* — `UnitActor.cpp`, in `AUnitActor::InitFor`, replacing the
unit-branch body (the part that picks aircraft vs boat):

```cpp
		const FUnitTpl* T = G.Content.Unit(G.Players[OwnerPlayer].FactionId, U.Tpl);
		DisplayName = T ? T->Name : TEXT("Unit");
		bIsAir = U.Domain == EUnitDomain::Air;

		bool bCustomMesh = false;
		if (T && !T->MeshPath.IsEmpty())
		{
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *T->MeshPath))
			{
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
				C->SetStaticMesh(Mesh);
				C->SetRelativeRotation(FRotator(0.f, T->MeshYawDeg, 0.f));
				C->SetWorldScale3D(FVector(T->MeshScale));
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetupAttachment(Root);
				C->RegisterComponent();

				const FBoxSphereBounds Bounds = Mesh->GetBounds();
				RingRadius = Bounds.SphereRadius * T->MeshScale * 1.05f;
				const float LengthUU = Bounds.BoxExtent.X * 2.f * T->MeshScale;
				BobDamp = bIsAir ? 0.f : FMath::Clamp(900.f / LengthUU, 0.35f, 1.f);
				bCustomMesh = true;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Custom mesh not found for %s: %s (using procedural hull)"),
					*U.Tpl.ToString(), *T->MeshPath);
			}
		}

		if (!bCustomMesh)
		{
			if (bIsAir) { BuildAircraft(700.f, Color); }
			else { /* ...the existing tier + BuildBoat code, unchanged... */ }
		}
```

*Data* — `Content/Data/dominion.json`:

```json
"patrol_boat": {
  "name": "Patrol Boat", "class": "naval", "hp": 260, "armor": 2,
  "speed": 3.4, "vision": 8, "cost": 65, "buildTime": 10,
  "mesh": "/Game/Models/ship_dark.ship_dark", "meshScale": 4.5, "meshYaw": -90,
  "weapons": [ ... unchanged ... ]
}
```

Grading yourself: (a) does a deliberately wrong path log the warning
and show the old wedge hull? (b) does clicking the new model select it,
with a sensibly sized ring? (c) does it still bob on the waves? It
should — `UpdateVisual` moves the *actor*, and it neither knows nor
cares which components hang off the root. That indifference is the
component pattern from lesson 7 paying rent.

**10.3 — The gremlin hunt.**
Scale arithmetic, using Kenney's `ship_dark` as the example: the editor
shows (or `GetBounds()` reports) BoxExtent ≈ (100, 55, 90) — remember
those are **half**-sizes, so the model is ~200 units long. Target
length 900 units → `meshScale = 900 / 200 = 4.5`. For the yaw: Kenney's
ships face **−Y**, and our forward is **+X**, so the model needs
`meshYaw: -90` — but every pack differs; the honest method is trying
`90`, seeing it sail backwards, and flipping the sign. (You'll know
it's wrong because boats visibly *reverse* into battle — W drives the
sim's +facing regardless of how the costume points.)

**10.4 — Defending the architecture.**
The mesh fields are *reference data about content*, not *simulation
state*: nothing in `Sim/Systems.cpp` ever reads them, so they can't
influence positions, damage, or the RNG — the same argument that lets
fish exist (design rule 18). The ContentDB already carries
render-facing data (display names, faction colors); a mesh path is no
different in kind. The concrete test: run two `-Seed=7` matches, one
with the mesh installed and one with the path deliberately broken (so
one game shows the model and the other shows the fallback wedge), and
diff the periodic `StateHash()` logs — identical hashes prove the
model cannot desync anything.

**10.5 (stretch) — OBJ loader sketch.**

```cpp
// Parse: lines "v x y z" are vertices; "f a b c" are 1-based indices.
// (Real OBJ 'f' entries can look like "5/2/7" — take the part before '/'.)
bool LoadObjFile(const FString& Path, TArray<FVector>& OutVerts, TArray<int32>& OutTris)
{
	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *Path)) { return false; }
	for (const FString& Line : Lines)
	{
		TArray<FString> Tok;
		Line.ParseIntoArrayWS(Tok);
		if (Tok.Num() >= 4 && Tok[0] == TEXT("v"))
		{
			// OBJ is Y-up right-handed; UE is Z-up left-handed: swap Y/Z.
			OutVerts.Add(FVector(FCString::Atof(*Tok[1]),
				FCString::Atof(*Tok[3]), FCString::Atof(*Tok[2])) * 100.f);
		}
		else if (Tok.Num() >= 4 && Tok[0] == TEXT("f"))
		{
			auto Index = [&Tok](int32 i)
			{
				FString Head;
				Tok[i].Split(TEXT("/"), &Head, nullptr);
				return FCString::Atoi(Head.IsEmpty() ? *Tok[i] : *Head) - 1;
			};
			// Fan-triangulate polygons with >3 corners.
			for (int32 i = 2; i + 1 < Tok.Num(); ++i)
			{
				OutTris.Append({ Index(1), Index(i), Index(i + 1) });
			}
		}
	}
	return OutVerts.Num() > 0 && OutTris.Num() > 0;
}
```

Feed the arrays to `Hull->CreateMeshSection_LinearColor` (compute one
normal per face like `FMeshBuilder::Tri` does — or pass an empty
normals array and accept soft shading), tint with `MakeMID`, done.
Watch the winding: if your model renders inside-out, reverse each
triangle's index order — you debugged exactly this bug on the
procedural hulls, so you know the drill. Congratulations: you've now
walked the same road the engine's FBX importer walks, just 100,000
lines shorter.
