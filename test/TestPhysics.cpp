// Collision physics solver (PhysicsWorld.cpp): shapes, contacts, resting,
// bouncing, friction, characters, kinematic pushers and triggers.
//
//   cmake --build build --target TestPhysics && ./build/bin/TestPhysics

#include "../include/core_engine/Physics.hpp"
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

using namespace ilmeee;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    ++g_checks;                                                                \
    if (!(cond)) {                                                             \
      ++g_failures;                                                            \
      std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                          \
  } while (0)

static bool Near(float a, float b, float eps = 0.02f) {
  return std::fabs(a - b) <= eps;
}

static PhysicsShape Box(glm::vec3 center, glm::vec3 half) {
  PhysicsShape s;
  s.shape = ColliderShape::Box;
  s.center = center;
  s.halfExtents = half;
  return s;
}

static PhysicsShape Sphere(glm::vec3 center, float radius) {
  PhysicsShape s;
  s.shape = ColliderShape::Sphere;
  s.center = center;
  s.radius = radius;
  return s;
}

static PhysicsShape Capsule(glm::vec3 center, float radius, float halfSegment) {
  PhysicsShape s;
  s.shape = ColliderShape::Capsule;
  s.center = center;
  s.radius = radius;
  s.halfSegment = halfSegment;
  return s;
}

static PhysicsBody Body(size_t owner, BodyType type, PhysicsShape shape) {
  PhysicsBody b;
  b.owner = owner;
  b.type = type;
  b.shape = shape;
  if (type == BodyType::Dynamic) b.gravity = {0.0f, -9.81f, 0.0f};
  return b;
}

// Floor: 20 x 1 x 20 box whose top face is y = 0.
static PhysicsBody Floor(size_t owner = 0) {
  return Body(owner, BodyType::Static, Box({0, -0.5f, 0}, {10, 0.5f, 10}));
}

static void Run(PhysicsWorld &world, float seconds) {
  for (int i = 0; i < (int)(seconds * 60.0f); ++i) world.Step(1.0f / 60.0f);
}

int main() {
  // 1. Contacts per shape pair, normal from a to b.
  {
    Contact c;
    CHECK(Collide(Sphere({0, 0, 0}, 1), Sphere({1.5f, 0, 0}, 1), c));
    CHECK(Near(c.depth, 0.5f, 1e-4f) && Near(c.normal.x, 1, 1e-4f));
    CHECK(!Collide(Sphere({0, 0, 0}, 1), Sphere({3, 0, 0}, 1), c));
    // Within the margin: touching, negative depth.
    CHECK(Collide(Sphere({0, 0, 0}, 1), Sphere({2.01f, 0, 0}, 1), c, 0.02f));
    CHECK(c.depth < 0.0f);

    CHECK(Collide(Box({0, 0, 0}, {1, 1, 1}), Sphere({0, 1.5f, 0}, 1), c));
    CHECK(Near(c.depth, 0.5f, 1e-4f) && Near(c.normal.y, 1, 1e-4f));
    CHECK(Collide(Sphere({0, 1.5f, 0}, 1), Box({0, 0, 0}, {1, 1, 1}), c));
    CHECK(Near(c.normal.y, -1, 1e-4f));
    // Sphere centre inside the box: still pushed out the nearest face.
    CHECK(Collide(Box({0, 0, 0}, {2, 1, 2}), Sphere({0, 0.8f, 0}, 0.5f), c));
    CHECK(Near(c.normal.y, 1, 1e-4f) && Near(c.depth, 0.7f, 1e-4f));

    CHECK(Collide(Box({0, 0, 0}, {1, 1, 1}), Box({0, 1.8f, 0}, {1, 1, 1}), c));
    CHECK(Near(c.depth, 0.2f, 1e-4f) && Near(c.normal.y, 1, 1e-4f));
    CHECK(!Collide(Box({0, 0, 0}, {1, 1, 1}), Box({2.5f, 0, 0}, {1, 1, 1}), c));
    // A box turned 45° about Y: its corner reaches x = 1 + sqrt(2).
    PhysicsShape turned = Box({2.3f, 0, 0}, {1, 1, 1});
    turned.axes = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), {0, 1, 0}));
    CHECK(Collide(Box({0, 0, 0}, {1, 1, 1}), turned, c));
    CHECK(c.normal.x > 0.9f);

    // Upright capsule standing into a floor; lying capsule beside a wall.
    CHECK(Collide(Box({0, -0.5f, 0}, {5, 0.5f, 5}), Capsule({0, 0.9f, 0}, 0.3f, 0.7f), c));
    CHECK(Near(c.normal.y, 1, 1e-4f) && Near(c.depth, 0.1f, 1e-4f));
    CHECK(Collide(Capsule({0, 0, 0}, 0.5f, 1.0f), Capsule({0.8f, 0.5f, 0}, 0.5f, 1.0f), c));
    CHECK(Near(c.normal.x, 1, 1e-4f) && Near(c.depth, 0.2f, 1e-4f));
  }

  // 2. World shapes follow the object's transform and scale.
  {
    ColliderComponent col;
    col.enabled = true;
    col.center = {0, 0.5f, 0};
    col.size = {1, 1, 1};
    glm::mat4 M = glm::translate(glm::mat4(1.0f), {1, 2, 3});
    M = glm::scale(M, {2, 3, 4});
    PhysicsShape s = MakeWorldShape(col, M);
    CHECK(Near(s.center.y, 3.5f, 1e-4f) && Near(s.center.x, 1, 1e-4f));
    CHECK(Near(s.halfExtents.x, 1, 1e-4f) && Near(s.halfExtents.y, 1.5f, 1e-4f) &&
          Near(s.halfExtents.z, 2, 1e-4f));
    col.shape = ColliderShape::Capsule;
    col.radius = 0.5f;
    col.height = 2.0f;
    s = MakeWorldShape(col, M);
    CHECK(Near(s.radius, 2.0f, 1e-4f));               // max(sx, sz) * r
    CHECK(Near(s.halfSegment, 1.0f, 1e-4f));          // 2 * 0.5 * 3 - 2
    glm::vec3 lo, hi;
    ShapeBounds(s, lo, hi);
    CHECK(Near(hi.y - lo.y, 6.0f, 1e-4f));
  }

  // 3. Fitting: a box takes the bounds; a flat plane gets thickness below.
  {
    ColliderComponent col;
    FitColliderToBounds(col, {-1, 0, -2}, {1, 2, 2});
    CHECK(col.center == glm::vec3(0, 1, 0) && col.size == glm::vec3(2, 2, 4));
    FitColliderToBounds(col, {-5, 0, -5}, {5, 0, 5});
    CHECK(Near(col.center.y + col.size.y * 0.5f, 0.0f, 1e-5f)); // top = surface
    CHECK(col.size.y > 0.05f);
    col.shape = ColliderShape::Capsule;
    FitColliderToBounds(col, {-0.25f, 0, -0.25f}, {0.25f, 1.8f, 0.25f});
    CHECK(Near(col.radius, 0.25f, 1e-5f) && Near(col.height, 1.8f, 1e-5f));
    col.friction = NAN;
    col.size = {-2, 1, 1};
    col.Clamp();
    CHECK(col.friction == ColliderComponent().friction && col.size.x == 2.0f);
  }

  // 4. A dropped ball lands on the floor and rests there, grounded.
  {
    PhysicsWorld world;
    world.bodies = {Floor(), Body(1, BodyType::Dynamic, Sphere({0, 3, 0}, 0.5f))};
    Run(world, 3.0f);
    const PhysicsBody &ball = world.bodies[1];
    CHECK(Near(ball.shape.center.y, 0.5f));
    CHECK(std::fabs(ball.velocity.y) < 0.05f);
    CHECK(ball.grounded);
    CHECK(world.Collisions().size() == 1);
    CHECK(world.Collisions()[0].first == 0 && world.Collisions()[0].second == 1);
    CHECK(world.bodies[0].shape.center.y == -0.5f); // static never moves
  }

  // 5. Box stacks rest on each other.
  {
    PhysicsWorld world;
    world.bodies = {Floor(), Body(1, BodyType::Dynamic, Box({0, 1, 0}, {0.5f, 0.5f, 0.5f})),
                    Body(2, BodyType::Dynamic, Box({0.1f, 2.5f, 0}, {0.5f, 0.5f, 0.5f}))};
    Run(world, 3.0f);
    CHECK(Near(world.bodies[1].shape.center.y, 0.5f, 0.03f));
    CHECK(Near(world.bodies[2].shape.center.y, 1.5f, 0.05f));
  }

  // 6. Bounciness sends a ball back up; zero bounciness does not.
  {
    PhysicsWorld world;
    PhysicsBody ball = Body(1, BodyType::Dynamic, Sphere({0, 3, 0}, 0.5f));
    ball.bounciness = 0.8f;
    world.bodies = {Floor(), ball};
    bool wentUp = false;
    for (int i = 0; i < 120; ++i) {
      world.Step(1.0f / 60.0f);
      if (world.bodies[1].velocity.y > 2.0f) wentUp = true;
    }
    CHECK(wentUp);
    PhysicsWorld dull;
    dull.bodies = {Floor(), Body(1, BodyType::Dynamic, Sphere({0, 3, 0}, 0.5f))};
    bool dullUp = false;
    for (int i = 0; i < 120; ++i) {
      dull.Step(1.0f / 60.0f);
      if (dull.bodies[1].velocity.y > 0.5f) dullUp = true;
    }
    CHECK(!dullUp);
  }

  // 7. Friction slows a sliding box; no friction keeps it sliding.
  {
    const auto slide = [](float friction) {
      PhysicsWorld world;
      PhysicsBody floor = Floor();
      floor.friction = friction;
      PhysicsBody box = Body(1, BodyType::Dynamic, Box({0, 0.5f, 0}, {0.5f, 0.5f, 0.5f}));
      box.friction = friction;
      box.velocity = {5, 0, 0};
      world.bodies = {floor, box};
      Run(world, 1.0f);
      return world.bodies[1].velocity.x;
    };
    CHECK(slide(0.0f) > 4.9f);
    CHECK(slide(1.0f) < 0.5f);
  }

  // 8. A character walked into a wall is pushed back out, not grounded;
  //    standing on a box it is grounded.
  {
    PhysicsWorld world;
    world.bodies = {Body(0, BodyType::Static, Box({1.0f, 1, 0}, {0.5f, 1, 2})),
                    Body(1, BodyType::Character, Capsule({0.3f, 0.9f, 0}, 0.3f, 0.6f))};
    world.Step(1.0f / 60.0f);
    const PhysicsBody &c = world.bodies[1];
    CHECK(c.shape.center.x <= 0.2f + 0.01f);
    CHECK(Near(c.displacement.y, 0.0f, 1e-4f));
    CHECK(!c.grounded);

    PhysicsWorld top;
    top.bodies = {Body(0, BodyType::Static, Box({0, 0.5f, 0}, {1, 0.5f, 1})),
                  Body(1, BodyType::Character, Capsule({0, 1.85f, 0}, 0.3f, 0.6f))};
    top.Step(1.0f / 60.0f);
    CHECK(top.bodies[1].grounded);
    CHECK(Near(top.bodies[1].shape.center.y, 1.9f, 0.01f));
  }

  // 9. Kinematic pushes dynamic; heavy dynamic barely moves for light one.
  {
    PhysicsWorld world;
    PhysicsBody crate = Body(1, BodyType::Dynamic, Box({0.8f, 0, 0}, {0.5f, 0.5f, 0.5f}));
    crate.gravity = glm::vec3(0.0f);
    world.bodies = {Body(0, BodyType::Kinematic, Box({0, 0, 0}, {0.5f, 0.5f, 0.5f})), crate};
    world.Step(1.0f / 60.0f);
    CHECK(world.bodies[0].shape.center.x == 0.0f);
    CHECK(world.bodies[1].shape.center.x >= 0.99f);

    PhysicsWorld split;
    PhysicsBody heavy = Body(0, BodyType::Dynamic, Sphere({0, 0, 0}, 0.5f));
    PhysicsBody light = Body(1, BodyType::Dynamic, Sphere({0.8f, 0, 0}, 0.5f));
    heavy.gravity = light.gravity = glm::vec3(0.0f);
    heavy.inverseMass = 1.0f / 9.0f;
    light.inverseMass = 1.0f;
    split.bodies = {heavy, light};
    split.Step(1.0f / 60.0f);
    const float heavyMove = -split.bodies[0].displacement.x;
    const float lightMove = split.bodies[1].displacement.x;
    CHECK(heavyMove > 0.0f && Near(lightMove / heavyMove, 9.0f, 0.1f));
  }

  // 10. Triggers report overlaps and never push; static-static is ignored.
  {
    PhysicsWorld world;
    PhysicsBody zone = Body(0, BodyType::Static, Box({0, 0, 0}, {1, 1, 1}));
    zone.trigger = true;
    PhysicsBody player = Body(1, BodyType::Character, Sphere({0.5f, 0, 0}, 0.5f));
    PhysicsBody prop = Body(2, BodyType::Static, Sphere({-0.5f, 0, 0}, 0.3f));
    world.bodies = {zone, player, prop};
    world.Step(1.0f / 60.0f);
    CHECK(world.Triggers().size() == 1);
    CHECK(world.Triggers()[0].first == 0 && world.Triggers()[0].second == 1);
    CHECK(world.Collisions().empty());
    CHECK(world.bodies[1].displacement == glm::vec3(0.0f));
  }

  // 11. ignorePair (object + its own child) skips the pair.
  {
    PhysicsWorld world;
    world.bodies = {Floor(7), Body(8, BodyType::Dynamic, Sphere({0, 0.2f, 0}, 0.5f))};
    world.bodies[1].gravity = glm::vec3(0.0f);
    world.ignorePair = [](size_t a, size_t b) { return a + b == 15; };
    world.Step(1.0f / 60.0f);
    CHECK(world.bodies[1].displacement == glm::vec3(0.0f));
  }

  // 12. Wireframes: 12 box edges; round shapes produce closed line lists.
  {
    std::vector<glm::vec3> lines;
    AppendShapeWireframe(Box({0, 0, 0}, {1, 1, 1}), lines);
    CHECK(lines.size() == 24);
    lines.clear();
    AppendShapeWireframe(Capsule({0, 0, 0}, 0.5f, 1.0f), lines);
    CHECK(!lines.empty() && lines.size() % 2 == 0);
  }

  std::printf("TestPhysics: %d/%d checks passed\n", g_checks - g_failures,
              g_checks);
  return g_failures == 0 ? 0 : 1;
}
