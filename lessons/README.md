# Archipelago Command — The Course

A 9-lesson tour of how this game was made, for a Python-speaking
12-year-old with a C++-speaking Dad as co-pilot. Work through them in
order; each has hands-on challenges against the real codebase, and each
`lesson_N_solution.md` has full answers — wrestle first, peek second.

| # | Lesson | You'll learn |
|---|---|---|
| 1 | [The Big Picture](lesson_1.md) | Sim vs render, the game loop, commands, why determinism |
| 2 | [Python → C++ Survival Guide](lesson_2.md) | Types, headers, structs, `TArray`/`TMap`, pointers & references |
| 3 | [ECS: The Game's Skeleton](lesson_3.md) | Entities, components, systems; write your first system code |
| 4 | [The Clock and the Dice](lesson_4.md) | Fixed timestep, interpolation, seeded RNG, the state hash |
| 5 | [Finding the Way](lesson_5.md) | BFS, A\*, heuristics, path caching, group movement |
| 6 | [Data-Driven Design](lesson_6.md) | JSON content, combat math, invent & balance your own unit |
| 7 | [The TV Crew: Unreal Engine](lesson_7.md) | Actors, UCLASS/UPROPERTY, procedural meshes, materials |
| 8 | [The Enemy Commander](lesson_8.md) | Three-layer AI: strategic / operational / tactical |
| 9 | [Capstone: Build the Lighthouse](lesson_9.md) | A complete feature across sim + data + visuals |
| 10 | [Your Own Ship Models](lesson_10.md) | Importing real 3D assets (Blender/Kenney/Fab), licenses, data-driven mesh overrides |

**Ground rules:** rebuild after every C++ change (the command is in
lesson 1), revert experiments when a challenge says so, and if the game
ever behaves strangely, `git status` will show you what you've touched.

**🧠 Dad corner** boxes are for the co-pilot: C++ subtleties (references,
templates, Unreal's code-generation macros) worth a whiteboard minute.

By the end you'll be able to edit this game confidently, understand the
basics of programming on Unreal Engine, and recognize the C++ patterns
and algorithms — ECS, fixed ticks, A\*, seeded RNG, object pools —
that real games are built from.
