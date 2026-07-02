#pragma once

#include "CoreMinimal.h"

// World-space mapping for the render layer. The sim works in cell units;
// everything visual multiplies by AC_CELL. Sea level is Z=0.
inline constexpr float AC_CELL = 600.f;          // 1 sim cell = 6 m
inline constexpr float AC_AIR_ALTITUDE = 2600.f; // aircraft cruise height
inline constexpr float AC_SEA_LEVEL = 0.f;

inline FVector2D SimToWorld2D(const FVector2f& P) { return FVector2D(P.X * AC_CELL, P.Y * AC_CELL); }
inline FVector2f WorldToSim2D(const FVector& P) { return FVector2f(float(P.X / AC_CELL), float(P.Y / AC_CELL)); }

// Engine built-in assets used everywhere (no project content required).
#define AC_MESH_CUBE     TEXT("/Engine/BasicShapes/Cube.Cube")
#define AC_MESH_SPHERE   TEXT("/Engine/BasicShapes/Sphere.Sphere")
#define AC_MESH_CYLINDER TEXT("/Engine/BasicShapes/Cylinder.Cylinder")
#define AC_MESH_CONE     TEXT("/Engine/BasicShapes/Cone.Cone")
#define AC_MAT_BASIC     TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")
