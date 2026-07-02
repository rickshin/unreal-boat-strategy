# Lesson 2 — Python → C++ Survival Guide

You know Python. C++ does the same jobs — variables, functions, loops,
classes — but it makes you *declare your intentions up front* and it
gets **compiled** (translated to machine code) before it runs. That's
why it's fast enough to update thousands of things 20 times a second.

## The same program, twice

**Python:**
```python
def unit_value(cost_wood, cost_iron):
    return cost_wood + cost_iron

speed = 3.4
names = ["Patrol Boat", "Destroyer"]
stats = {"hp": 260, "armor": 2}
```

**C++ (the way this game writes it):**
```cpp
int32 UnitValue(int32 CostWood, int32 CostIron)
{
    return CostWood + CostIron;
}

float Speed = 3.4f;
TArray<FString> Names = { TEXT("Patrol Boat"), TEXT("Destroyer") };
TMap<FString, int32> Stats = { { TEXT("hp"), 260 }, { TEXT("armor"), 2 } };
```

Spot the differences:

1. **Every variable has a type**: `int32` (whole number), `float`
   (decimal — note the `f` on `3.4f`), `bool`, `FString` (text).
   Python guesses the type at runtime; C++ wants it in writing, and the
   compiler *checks your work* before the game ever runs.
2. **`;` ends every statement** and **`{ }` replaces indentation**.
   (You still indent — but for humans, not for the compiler.)
3. Python's `list` → Unreal's **`TArray`**. Python's `dict` → Unreal's
   **`TMap`**. Same ideas, same jobs:

```cpp
Names.Add(TEXT("Battleship"));      // names.append("Battleship")
Names.Num();                        // len(names)
Stats.Contains(TEXT("hp"));         // "hp" in stats
Stats[TEXT("hp")] = 300;            // stats["hp"] = 300
for (const FString& N : Names)      // for n in names:
{
    ...
}
```

## Two files per idea: `.h` and `.cpp`

C++ code is split like a restaurant:

- **`.h` (header) = the menu.** It *declares* what exists: "there is a
  struct `FSimMap` with a function `Generate(Seed)`." Short, no cooking.
- **`.cpp` = the kitchen.** It *defines* how things actually work — the
  full recipe for `Generate`.

Other files that want to use the map write `#include "SimMap.h"` — they
read the menu, they don't need the kitchen. Look at
`Sim/SimMap.h` (menu) next to `Sim/SimMap.cpp` (kitchen) and you'll see
the pattern instantly.

## `struct` = Python class that's just data

The game's components are structs:

```cpp
struct FHealthC
{
    float Hp = 100.f;      // fields with default values,
    float MaxHp = 100.f;   // like a Python dataclass
    int32 Armor = 0;
};
```

Naming code used everywhere in Unreal: structs start with `F`, classes
that are actors start with `A`, `b` prefixes a bool (`bDefeated`),
and functions/variables are `CapitalizedLikeThis`.

## The `&` and `*` you'll see everywhere

🧠 **Dad corner** — worth 10 minutes of whiteboard time:
Python variables are always *references to* objects. C++ variables hold
the object *itself* by default, and copies are real copies. So C++ has
special syntax for "don't copy, point at the original":

- `FHealthC& H` — a **reference**: another name for the same object.
  `H.Hp -= 10` damages the *real* component.
- `const FSimMap& Map` — a reference you promise **not to modify**
  (`const` = "look, don't touch"). The compiler enforces the promise.
- `FHealthC* H` — a **pointer**: like a reference but it can be
  "nothing" (`nullptr`). This game uses pointers mostly for *maybe*
  answers: `World.Health.Find(Id)` returns a pointer that's `nullptr`
  when entity `Id` has no health component. Always check before using:

```cpp
FHealthC* H = World.Health.Find(Id);   // like dict.get(id) in Python
if (H) { H->Hp -= 10.f; }              // -> instead of . through a pointer
```

## Compile errors are your friends

In Python, a typo explodes at runtime, maybe an hour into a match. In
C++, the compiler catches it before the game starts and tells you the
file and line. You'll make lots of errors this week. **Read the first
error only** (the rest are often echoes), fix it, rebuild.

---

## 🔧 Challenges

Rebuild + run after each change (command in lesson 1).

**Challenge 2.1 — Rich kid mode.**
Find where a player's starting Wood (200) and Iron (100) are set (hint:
it's a struct with default values in `Sim/SimGame.h`). Change them to
1000 each, rebuild, and confirm in the top bar of the game. Then put
them back — the game is balanced around the original numbers.

**Challenge 2.2 — Your first log line.**
In `AACGameMode::StartMatch` (in `ACGameMode.cpp`) there's already a
`UE_LOG` line printing the seed. Add your own right after it that prints
which faction the local player is, like: `Playing as: dominion`.
Hints: the faction id is `SimGame->Players[0].FactionId` (an `FName`),
and to print an FName inside `TEXT("...%s...")` you pass
`*FactionId.ToString()`. Check `Saved/Logs/ArchipelagoCommand.log` for
your line.

**Challenge 2.3 — Translate from Python.**
Translate this Python into C++ using `TArray` (just write it on paper or
in a scratch file — you don't need to wire it into the game):

```python
def count_cheap(costs, limit):
    n = 0
    for c in costs:
        if c < limit:
            n += 1
    return n
```

**Challenge 2.4 — Bug hunt.**
Each of these three lines has one classic C++ mistake Python kids make.
Find them (answers in the solution):

```cpp
float Speed = 3.4        // (a)
if (Hp = 0) { Die(); }   // (b)
FHealthC* H = World.Health.Find(Id);
H->Hp = 50.f;            // (c)
```
