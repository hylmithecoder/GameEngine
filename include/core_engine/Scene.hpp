#pragma once
#include <string>
#include <vector>

// Simple Scene object structure
struct GameObject {
  std::string name;
  float x, y;
  float width, height;
  std::string spritePath;
  float rotation = 0.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;

  // Component: Physics
  bool hasPhysics = false;
  bool useGravity = true;
  bool isKinematic = false;
  float mass = 1.0f;
  float drag = 0.0f;
  float gravityY = -9.81f;

  // Component: Audio
  bool hasAudio = false;
  std::string audioPath = "";
  bool isPlaying = false;
};

struct Scene {
  std::string sceneName;
  std::vector<GameObject> objects;
};