# Lesson 2 — Solutions

**2.1 — Rich kid mode.**
In `Sim/SimGame.h`, struct `FSimPlayer`:

```cpp
struct FSimPlayer
{
    FName FactionId;
    float Wood = 1000.f;   // was 200.f
    float Iron = 1000.f;   // was 100.f
    ...
```

Rebuild, run, and the top bar shows `Wood 1000  Iron 1000`. Notice the
AI got rich too — the defaults apply to *both* players, because both are
`FSimPlayer`s. (Remember to change it back!)

**2.2 — Your first log line.**

```cpp
UE_LOG(LogACGame, Log, TEXT("Match started: seed %llu, hash %llu"), Seed, SimGame->StateHash());
UE_LOG(LogACGame, Log, TEXT("Playing as: %s"),
    *SimGame->Players[0].FactionId.ToString());
```

Two things to notice: `%s` in Unreal logs expects a `TCHAR*` string, and
the `*` in front of `.ToString()` converts Unreal's `FString` into
exactly that. (Yes — that `*` is a different use of the star than a
pointer. C++ reuses symbols; context tells you which meaning applies.)

**2.3 — Translate from Python.**

```cpp
int32 CountCheap(const TArray<int32>& Costs, int32 Limit)
{
    int32 N = 0;
    for (int32 C : Costs)
    {
        if (C < Limit)
        {
            ++N;
        }
    }
    return N;
}
```

Grading yourself: full marks if you (a) gave every variable a type,
(b) took the array as `const TArray<int32>&` — passing by const
reference avoids copying the whole array (Dad can explain why plain
`TArray<int32> Costs` would copy), and (c) remembered `return` needs a
semicolon.

**2.4 — Bug hunt.**

- **(a)** Missing semicolon — and strictly `3.4` is a `double`;
  Unreal code writes `3.4f` for a `float`. Fix: `float Speed = 3.4f;`
- **(b)** `=` is assignment, `==` is comparison! `if (Hp = 0)` *sets*
  Hp to zero and then tests it (always false). This bug has sunk real
  ships. Fix: `if (Hp == 0)`. (This is why compilers warn about it —
  never ignore that warning.)
- **(c)** No null check. `Find` returns `nullptr` when the entity has
  no health component, and writing through a null pointer crashes the
  whole game. Fix:

```cpp
FHealthC* H = World.Health.Find(Id);
if (H) { H->Hp = 50.f; }
```

Search `Systems.cpp` for `.Find(` and you'll see the game checks the
result virtually every time. That's not paranoia; that's the habit.
